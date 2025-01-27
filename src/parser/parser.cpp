#include "csrt/parser/parser.hpp"

#include <algorithm>
#include <exception>
#include <filesystem>
#include <unordered_map>

#include <pugixml.hpp>

#include "csrt/parser/ior_lut.hpp"
#include "csrt/parser/medium_lut.hpp"
#include "csrt/parser/model_loader.hpp"
#include "csrt/parser/sun_sky.hpp"

namespace
{

using namespace csrt;

namespace local
{
    RendererConfig config;
    std::string current_directory;
    std::unordered_map<std::string, std::string> map_default;
    std::unordered_map<std::string, uint64_t> map_texture;
    std::unordered_map<std::string, uint64_t> map_bsdf;
    std::unordered_map<std::string, uint64_t> map_medium;
} // namespace local

} // namespace

namespace element_parser
{

using namespace csrt;

void ReadCamera(const pugi::xml_node &sensor_node);

void ReadIntegrator(const pugi::xml_node &integrator_node);

uint64_t ReadTexture(const pugi::xml_node &texture_node, const float scale,
                     const float defalut_value);
uint64_t ReadTexture(const pugi::xml_node &parent_node,
                     const std::vector<std::string> &valid_names,
                     const float defalut_value);
uint64_t ReadBitmap(const std::string &filename, const std::string &id,
                    float gamma, float scale, int *width_max);

uint64_t ReadMedium(const pugi::xml_node &medium_node);

uint64_t ReadBsdf(const pugi::xml_node &bsdf_node, std::string id,
                  uint64_t id_opacity, uint64_t id_bumpmap, bool twosided);
float ReadDielectricIor(const pugi::xml_node &parent_node,
                        const std::vector<std::string> &valid_names,
                        float defalut_value);
void ReadConductorIor(const pugi::xml_node &parent_node, Vec3 *eta, Vec3 *k);

uint64_t ReadShape(const pugi::xml_node &shape_node);

void ReadEmitter(const pugi::xml_node &emitter_node);

} // namespace element_parser

namespace basic_parser
{

using namespace csrt;

bool GetChildNodeByName(const pugi::xml_node &parent_node,
                        const std::vector<std::string> &valid_names,
                        pugi::xml_node *child_node);
bool ReadBoolean(const pugi::xml_node &parent_node,
                 const std::vector<std::string> &valid_names,
                 const bool defalut_value);
int ReadInt(const pugi::xml_node &parent_node,
            const std::vector<std::string> &valid_names,
            const int defalut_value);
float ReadFloat(const pugi::xml_node &parent_node,
                const std::vector<std::string> &valid_names,
                const float defalut_value);
Vec3 ReadVec3(const pugi::xml_node &parent_node,
              const std::vector<std::string> &valid_names,
              const Vec3 &defalut_value);
Vec3 ReadVec3(const pugi::xml_node &vec3_node, const Vec3 &defalut_value,
              std::string value_name);
Mat4 ReadMat4(const pugi::xml_node &matrix_node);
Mat4 ReadTransform4(const pugi::xml_node &transform_node);

} // namespace basic_parser

namespace csrt
{

RendererConfig LoadConfig(const std::string &filename)
{
    local::config = {};

    //
    // 确保XML文件的路径正确
    //
    if (!std::filesystem::exists(std::filesystem::path{filename}))
    {
        std::string info = "cannot find config file: '" + filename + ".";
        throw MyException(info.c_str());
    }
    local::current_directory = GetDirectory(filename);

    //
    // 解析 XML
    //
    fprintf(stderr, "[info] read config file: '%s'.\n", filename.c_str());
    if (GetSuffix(filename) != "xml")
    {
        throw MyException(
            "[error] only support mitsuba xml format config file.\n");
    }
    pugi::xml_document doc;
    if (!doc.load_file(filename.c_str()))
    {
        throw MyException("[error] read config file failed.\n");
    }
    pugi::xml_node scene_node = doc.child("scene");

    //
    // 解析一些预先定义的参数
    //
    local::map_default = {};
    for (pugi::xml_node node : scene_node.children("default"))
    {
        std::string name = node.attribute("name").value();
        std::string value = node.attribute("value").value();
        local::map_default["$" + name] = value;
    }

    //
    // 解析相机参数
    //
    element_parser::ReadCamera(scene_node.child("sensor"));

    //
    // 解析光线跟踪算法参数
    //
    element_parser::ReadIntegrator(scene_node.child("integrator"));

    //
    // 解析纹理
    //
    local::map_texture = {};
    for (pugi::xml_node texture_node : scene_node.children("texture"))
        element_parser::ReadTexture(texture_node, 1.0f, 1.0f);

    //
    // 解析 BSDF
    //
    local::map_bsdf = {};
    for (pugi::xml_node bsdf_node : scene_node.children("bsdf"))
        element_parser::ReadBsdf(bsdf_node, "", kInvalidId, kInvalidId, false);

    //
    // 解析参与介质
    //
    local::map_medium = {};
    for (pugi::xml_node medium_node : scene_node.children("medium"))
        element_parser::ReadMedium(medium_node);

    //
    // 解析物体
    //
    for (pugi::xml_node shape_node : scene_node.children("shape"))
        element_parser::ReadShape(shape_node);

    //
    // 解析除了面光源之外的其它光源
    //
    for (pugi::xml_node emitter_node : scene_node.children("emitter"))
        element_parser::ReadEmitter(emitter_node);

    return local::config;
}

} // namespace csrt

void element_parser::ReadCamera(const pugi::xml_node &sensor_node)
{
    std::string type = sensor_node.attribute("type").as_string();
    if (type != "perspective")
    {
        throw MyException("[error] only support 'perspective' sensor.\n");
    }
    //
    // 读取绘制图像的分辨率
    //
    int width = 768, height = 576;
    for (pugi::xml_node node : sensor_node.child("film").children("integer"))
    {
        switch (Hash(node.attribute("name").value()))
        {
        case "width"_hash:
        {
            std::string value_str = node.attribute("value").value();
            if (!value_str.empty())
            {
                if (value_str[0] == '$')
                {
                    if (!local::map_default.count(value_str))
                    {
                        fprintf(stderr,
                                "[error] cannot find '%s' from config file.\n",
                                value_str.c_str());
                        exit(1);
                    }
                    value_str = local::map_default.at(value_str);
                }
                width = std::stoi(value_str);
            }
            break;
        }
        case "height"_hash:
        {
            std::string value_str = node.attribute("value").value();
            if (!value_str.empty())
            {
                if (value_str[0] == '$')
                {
                    if (!local::map_default.count(value_str))
                    {
                        fprintf(stderr,
                                "[error] cannot find '%s' from config file.\n",
                                value_str.c_str());
                        exit(1);
                    }
                    value_str = local::map_default.at(value_str);
                }
                height = std::stoi(value_str);
            }
            break;
        }
        default:
            break;
        }
    }
    local::config.camera.width = width;
    local::config.camera.height = height;

    //
    // 读取绘制图像在水平方向的可视角度
    //
    float focal_length = 50.0f;
    std::string fov_axis = "x";
    for (pugi::xml_node node : sensor_node.children("string"))
    {
        switch (Hash(node.attribute("name").value()))
        {
        case "focalLength"_hash:
        {
            std::string foval_length_str = node.attribute("value").as_string();
            foval_length_str =
                foval_length_str.substr(0, foval_length_str.size() - 2);
            focal_length = std::stof(foval_length_str);
            break;
        }
        case "fovAxis"_hash:
        {
            fov_axis = node.attribute("value").as_string();
        }
        }
    }
    float fov_x = basic_parser::ReadFloat(sensor_node, {"fov"}, -1.0f);
    switch (Hash(fov_axis.c_str()))
    {
    case "x"_hash:
    {
        if (fov_x <= 0.0f)
            fov_x =
                2.0f * atanf(36.0f * 0.5f / focal_length) * 180.0f * k1DivPi;
        break;
    }
    case "y"_hash:
    {
        if (fov_x <= 0.0f)
            fov_x =
                2.0f * atanf(24.0f * 0.5f / focal_length) * 180.0f * k1DivPi;
        fov_x = fov_x * width / height;
        break;
    }
    case "smaller"_hash:
    {
        if (width > height)
        {
            if (fov_x <= 0.0f)
                fov_x = 2.0f * atanf(24.0f * 0.5f / focal_length) * 180.0f *
                        k1DivPi;
            fov_x = fov_x * width / height;
        }
        break;
    }
    default:
    {
        std::string info =
            "[error] unsupport fov axis type '" + fov_axis + "'\n";
        throw MyException(info.c_str());
        break;
    }
    }
    local::config.camera.fov_x = fov_x;

    //
    // 读取绘制图像每个像素样本的数量
    //
    int sample_count = 4;
    for (pugi::xml_node node : sensor_node.child("sampler").children("integer"))
    {
        switch (Hash(node.attribute("name").value()))
        {
        case "sampleCount"_hash:
        case "sample_count"_hash:
        {
            std::string value_str = node.attribute("value").value();
            if (!value_str.empty())
            {
                if (value_str[0] == '$')
                {
                    if (!local::map_default.count(value_str))
                    {
                        fprintf(stderr,
                                "[error] cannot find '%s' from config file.\n",
                                value_str.c_str());
                        exit(1);
                    }
                    value_str = local::map_default.at(value_str);
                }
                sample_count = std::stoi(value_str);
            }
            break;
        }
        default:
            break;
        }
    }
    local::config.camera.spp = sample_count;

    //
    // 读取相机的位置和朝向
    //
    Vec3 eye = {0.0f, 0.0f, 0.0f}, look_at = {0.0f, 0.0f, 1.0f},
         up = {0.0f, 1.0f, 0.0f};
    if (sensor_node.child("transform"))
    {
        const Mat4 to_world =
            basic_parser::ReadTransform4(sensor_node.child("transform"));
        eye = TransformPoint(to_world, eye);
        look_at = TransformPoint(to_world, look_at);
        up = TransformVector(to_world, up);
    }
    local::config.camera.eye = eye, local::config.camera.look_at = look_at,
    local::config.camera.up = up;
}

void element_parser::ReadIntegrator(const pugi::xml_node &integrator_node)
{

    uint32_t max_depth = kMaxUint, rr_depth = 5;
    for (pugi::xml_node node : integrator_node.children("integer"))
    {
        switch (Hash(node.attribute("name").value()))
        {
        case "maxDepth"_hash:
        case "max_depth"_hash:
        {
            std::string value = node.attribute("value").value();
            if (!value.empty())
            {
                if (value[0] == '$')
                {
                    if (!local::map_default.count(value))
                    {
                        throw MyException(
                            "cannot find default value for max depth.");
                    }
                    value = local::map_default.at(value);
                }
                max_depth = std::stoi(value);
            }
            break;
        }
        case "rrDepth"_hash:
        case "rr_depth"_hash:
        {
            std::string value = node.attribute("value").value();
            if (!value.empty())
            {
                if (value[0] == '$')
                {
                    if (!local::map_default.count(value))
                    {
                        throw MyException(
                            "cannot find default value for rr depth.");
                    }
                    value = local::map_default.at(value);
                }
                rr_depth = std::stoi(value);
            }
            break;
        }
        default:
            break;
        }
    }

    IntegratorInfo info;
    info.depth_max = max_depth;
    info.depth_rr = rr_depth;
    info.hide_emitters = basic_parser::ReadBoolean(
        integrator_node, {"hide_emitters", "hideEmitters"}, false);
    info.pdf_rr =
        basic_parser::ReadFloat(integrator_node, {"rr_pdf", "rrPdf"}, 0.95f);

    std::string integrator_type =
        integrator_node.attribute("type").as_string("path");
    if (!integrator_type.empty() && integrator_type[0] == '$' &&
        local::map_default.count(integrator_type))
    {
        integrator_type = local::map_default.at(integrator_type);
    }
    switch (Hash(integrator_type.c_str()))
    {
    case "volpath"_hash:
        info.type = IntegratorType::kVolPath;
        break;
    case "path"_hash:
        info.type = IntegratorType::kPath;
        break;
    default:
        fprintf(stderr, "unsupport integrator type '%s', use 'path' instead.\n",
                integrator_type.c_str());
        info.type = IntegratorType::kPath;
        break;
    }

    local::config.integrator = info;
}

uint64_t element_parser::ReadTexture(const pugi::xml_node &texture_node,
                                     const float scale,
                                     const float defalut_value)
{
    std::string id = texture_node.attribute("id").as_string();
    if (!texture_node)
    {
        const uint64_t index = local::config.textures.size();
        if (id.empty())
            id = "texture_" + std::to_string(index);
        local::map_texture[id] = index;

        TextureInfo info;
        info.type = TextureType::kConstant;
        info.constant.color = {scale * defalut_value};
        local::config.textures.push_back(info);
        return index;
    }

    uint64_t id_texture = kInvalidId;
    std::string node_name = texture_node.name();
    switch (Hash(node_name.c_str()))
    {
    case "scale"_hash:
    {
        const float local_scale =
            basic_parser::ReadFloat(texture_node, {"scale"}, 1.0f);

        id_texture = ReadTexture(texture_node.child("texture"),
                                 scale * local_scale, defalut_value);

        break;
    }
    case "ref"_hash:
    {
        if (!local::map_texture.count(id))
        {
            std::string info =
                "[error] cannot find texture with id '" + id + "'.\n";
            throw MyException(info.c_str());
        }
        id_texture = local::map_texture.at(id);
        break;
    }
    case "rgb"_hash:
    {
        const uint64_t index = local::config.textures.size();
        if (id.empty())
            id = "texture_" + std::to_string(index);
        const Vec3 value =
            basic_parser::ReadVec3(texture_node, Vec3(defalut_value), "");
        local::map_texture[id] = index;
        TextureInfo info;
        info.type = TextureType::kConstant;
        info.constant.color = scale * value;
        local::config.textures.push_back(info);
        id_texture = index;
        break;
    }
    case "float"_hash:
    {
        const uint64_t index = local::config.textures.size();
        if (id.empty())
            id = "texture_" + std::to_string(index);
        const float value =
            texture_node.attribute("value").as_float(defalut_value);
        local::map_texture[id] = index;
        TextureInfo info;
        info.type = TextureType::kConstant;
        info.constant.color = {scale * value};
        local::config.textures.push_back(info);
        id_texture = index;
        break;
    }
    case "texture"_hash:
    {
        std::string texture_type = texture_node.attribute("type").value();
        switch (Hash(texture_type.c_str()))
        {
        case "checkerboard"_hash:
        {
            Vec3 color0 = {0.4f, 0.4f, 0.4f};
            pugi::xml_node node_tmp;
            if (basic_parser::GetChildNodeByName(texture_node, {"color0"},
                                                 &node_tmp))
            {
                std::string node_tmp_name = node_tmp.name();
                if (node_tmp_name != "rgb" && node_tmp_name != "float")
                {
                    throw MyException(
                        "not support texture inside 'checkerboard'");
                }
                color0 = basic_parser::ReadVec3(texture_node, {"color0"}, 0.4f);
            }

            Vec3 color1 = {0.2f, 0.2f, 0.2f};
            if (basic_parser::GetChildNodeByName(texture_node, {"color1"},
                                                 &node_tmp))
            {
                std::string node_tmp_name = node_tmp.name();
                if (node_tmp_name != "rgb" && node_tmp_name != "float")
                {
                    throw MyException(
                        "not support texture inside 'checkerboard'\n");
                }
                color1 = basic_parser::ReadVec3(texture_node, {"color1"}, 0.2f);
            }

            Mat4 to_uv =
                basic_parser::ReadTransform4(texture_node.child("transform"));
            float u_offset =
                      basic_parser::ReadFloat(texture_node, {"uoffset"}, 0.0f),
                  v_offset =
                      basic_parser::ReadFloat(texture_node, {"voffset"}, 0.0f),
                  u_scale =
                      basic_parser::ReadFloat(texture_node, {"uscale"}, 1.0f),
                  v_scale =
                      basic_parser::ReadFloat(texture_node, {"vscale"}, 1.0f);
            to_uv = Mul(Translate(Vec3{u_offset, v_offset, 0.0f}), to_uv);
            to_uv = Mul(Scale(Vec3{u_scale, v_scale, 1.0f}), to_uv);

            const uint64_t index = local::config.textures.size();
            if (id.empty())
                id = "texture_" + std::to_string(index);
            local::map_texture[id] = index;
            TextureInfo info;
            info.type = TextureType::kCheckerboard;
            info.checkerboard.color0 = scale * color0;
            info.checkerboard.color1 = scale * color1;
            info.checkerboard.to_uv = to_uv;
            local::config.textures.push_back(info);
            id_texture = index;
            break;
        }
        case "bitmap"_hash:
        {
            pugi::xml_node child_node;
            if (!basic_parser::GetChildNodeByName(texture_node, {"filename"},
                                                  &child_node))
                throw MyException(
                    "[error] cannot find filename for bitmap texture.\n");

            const float gamma =
                basic_parser::ReadFloat(texture_node, {"gamma"}, 0.0f);
            const uint64_t index = local::config.textures.size();
            if (id.empty())
                id = "texture_" + std::to_string(index);

            const std::string filename =
                child_node.attribute("value").as_string();
            id_texture = ReadBitmap(local::current_directory + filename, id,
                                    gamma, scale, nullptr);
            break;
        }
        default:
        {
            std::string info =
                "[error] unsupport texture type '" + texture_type + "'.\n";
            throw MyException(info.c_str());
            break;
        }
        }
        break;
    }
    default:
    {
        std::string info =
            "[error] unsupport texture type '" + node_name + "'.\n";
        throw MyException(info.c_str());
        break;
    }
    }
    return id_texture;
}

uint64_t
element_parser::ReadTexture(const pugi::xml_node &parent_node,
                            const std::vector<std::string> &valid_names,
                            const float defalut_value)
{
    uint64_t index;
    pugi::xml_node texture_node;
    if (basic_parser::GetChildNodeByName(parent_node, valid_names,
                                         &texture_node))
    {
        index = ReadTexture(texture_node, 1.0f, defalut_value);
    }
    else
    {
        index = local::config.textures.size();
        const std::string id = "texture_" + std::to_string(index);
        local::map_texture[id] = index;
        TextureInfo info;
        info.type = TextureType::kConstant;
        info.constant.color = {defalut_value};
        local::config.textures.push_back(info);
    }

    return index;
}

uint64_t element_parser::ReadBitmap(const std::string &filename,
                                    const std::string &id, float gamma,
                                    float scale, int *width_max)
{
    int width = 0, height = 0, channel = 1;
    float *raw_data =
        image_io::Read(filename, gamma, width_max, &width, &height, &channel);

    const uint32_t num_element =
        static_cast<uint32_t>(width) * height * channel;
    for (uint32_t i = 0; i < num_element; ++i)
        raw_data[i] *= scale;

    const uint64_t index = local::config.textures.size();
    local::map_texture[id] = index;

    TextureInfo info;
    info.type = TextureType::kBitmap;
    info.bitmap.data = std::vector<float>(raw_data, raw_data + num_element);
    SAFE_DELETE_ARRAY(raw_data);
    info.bitmap.width = width;
    info.bitmap.height = height;
    info.bitmap.channel = channel;
    info.bitmap.to_uv = {};
    local::config.textures.push_back(info);
    return index;
}

uint64_t element_parser::ReadMedium(const pugi::xml_node &medium_node)
{
    std::string id = medium_node.attribute("id").as_string();
    if (local::map_medium.count(id))
        return local::map_medium[id];

    const uint32_t id_medium = local::config.media.size();
    if (id.empty())
        id = "medium_" + std::to_string(id_medium);
    local::map_medium[id] = id_medium;

    std::string type = medium_node.attribute("type").as_string();
    if (type != "homogeneous")
    {
        std::ostringstream oss;
        oss << "unsupported  media'" << type << "'.";
        throw MyException(oss.str());
    }

    float scale = basic_parser::ReadFloat(medium_node, {"scale"}, 1.0f);

    Vec3 sigma_s, sigma_a;
    pugi::xml_node albedo_node;
    if (basic_parser::GetChildNodeByName(medium_node, {"albedo"}, &albedo_node))
    {
        pugi::xml_node sigma_t_node;
        if (!basic_parser::GetChildNodeByName(
                medium_node, {"sigma_t", "sigmaT"}, &sigma_t_node))
        {
            throw MyException(
                "'sigma_t' and 'albedo' must be provided at the same time.");
        }
        Vec3 albedo = basic_parser::ReadVec3(albedo_node, Vec3(0.75f), "");
        Vec3 sigma_t = basic_parser::ReadVec3(sigma_t_node, Vec3(1.0f), "");
        sigma_s = albedo * sigma_t;
        sigma_a = sigma_t - sigma_s;
    }

    pugi::xml_node sigma_a_node;
    if (basic_parser::GetChildNodeByName(medium_node, {"sigmaA"},
                                         &sigma_a_node))
    {
        pugi::xml_node sigma_s_node;
        if (!basic_parser::GetChildNodeByName(medium_node, {"sigmaS"},
                                              &sigma_s_node))
        {
            throw MyException(
                "'sigma_a' and 'sigma_s' must be provided at the same time.");
        }
        sigma_a = basic_parser::ReadVec3(sigma_a_node, Vec3(1.0f), "");
        sigma_s = basic_parser::ReadVec3(sigma_s_node, Vec3(1.0f), "");
    }

    MediumInfo info;
    info.type = MediumType::kHomogeneous;
    if (!albedo_node && !sigma_a_node)
    {
        std::string material_name =
            medium_node.child("string").attribute("value").as_string("skin1");
        Vec3 g;
        if (medium_lut::LookupHomogeneousMedium(material_name, &sigma_a,
                                                &sigma_s, &g))
        {
            info.homogeneous.sigma_a = sigma_a * scale;
            info.homogeneous.sigma_s = sigma_s * scale;
            info.phase_func.type = PhaseFunctionType::kHenyeyGreenstein;
            info.phase_func.g = g;
        }
        else if (medium_lut::LookupIsotropicHomogeneousMedium(
                     material_name, &sigma_a, &sigma_s))
        {
            info.homogeneous.sigma_a = sigma_a * scale;
            info.homogeneous.sigma_s = sigma_s * scale;
            info.phase_func.type = PhaseFunctionType::kIsotropic;
        }
        else
        {
            std::string info =
                "unsupport medium type '" + material_name + "'.\n";
            throw MyException(info);
        }
    }
    else
    {
        info.homogeneous.sigma_a = sigma_a * scale;
        info.homogeneous.sigma_s = sigma_s * scale;
        if (!medium_node.child("phase"))
        {
            info.phase_func.type = PhaseFunctionType::kIsotropic;
        }
        else
        {
            pugi::xml_node phase_fuction_node = medium_node.child("phase");
            std::string phase_fuction_type =
                phase_fuction_node.attribute("type").value();
            switch (Hash(phase_fuction_type.c_str()))
            {
            case "isotropic"_hash:
                info.phase_func.type = PhaseFunctionType::kIsotropic;
                break;
            case "hg"_hash:
                info.phase_func.type = PhaseFunctionType::kHenyeyGreenstein;
                info.phase_func.g =
                    basic_parser::ReadFloat(phase_fuction_node, {"g"}, 0);
                break;
            default:
                fprintf(stderr,
                        "[warning] unsupport phase function '%s',  use "
                        "'isotropic' instead.\n",
                        phase_fuction_type.c_str());
                info.phase_func.type = PhaseFunctionType::kIsotropic;
                break;
            }
        }
    }
    local::config.media.push_back(info);

    return id_medium;
}

uint64_t element_parser::ReadBsdf(const pugi::xml_node &bsdf_node,
                                  std::string id, uint64_t id_opacity,
                                  uint64_t id_bumpmap, bool twosided)
{
    if (id.empty())
        id = bsdf_node.attribute("id").as_string();
    std::string type = bsdf_node.attribute("type").as_string();

    switch (Hash(type.c_str()))
    {
    case "bumpmap"_hash:
    {
        id_bumpmap = ReadTexture(bsdf_node.child("texture"), 1.0f, 1.0f);
        return ReadBsdf(bsdf_node.child("bsdf"), id, id_opacity, id_bumpmap,
                        twosided);
    }
    case "mask"_hash:
    {
        id_opacity = ReadTexture(bsdf_node, {"opacity"}, 1.0f);
        return ReadBsdf(bsdf_node.child("bsdf"), id, id_opacity, id_bumpmap,
                        twosided);
    }
    case "twosided"_hash:
    {
        return ReadBsdf(bsdf_node.child("bsdf"), id, id_opacity, id_bumpmap,
                        true);
    }
    case "coating"_hash:
    case "roughcoating"_hash:
    case "phong"_hash:
    case "ward"_hash:
    case "mixturebsdf"_hash:
    case "blendbsdf"_hash:
    case "difftrans"_hash:
    case "hk"_hash:
    case "irawan"_hash:
    case "null"_hash:
    {
        std::string info = "[error] not support bsdf type '" + type + "'.\n";
        throw MyException(info.c_str());
    }
    }

    const uint64_t id_bsdf = local::config.bsdfs.size();
    if (id.empty())
        id = "bsdf_" + std::to_string(id_bsdf);

    BsdfInfo info;
    info.twosided = twosided;
    info.id_opacity = id_opacity;
    info.id_bump_map = id_bumpmap;

    bool is_thin_dielectric = false;
    switch (Hash(type.c_str()))
    {
    case "diffuse"_hash:
    {
        uint64_t id_reflectance = ReadTexture(bsdf_node, {"reflectance"}, 0.5f);
        info.type = BsdfType::kDiffuse;
        info.diffuse.id_diffuse_reflectance = id_reflectance;
        break;
    }
    case "roughdiffuse"_hash:
    {
        bool use_fast_aaprox =
            basic_parser::ReadBoolean(bsdf_node, {"useFastApprox"}, false);
        uint64_t id_reflectance = ReadTexture(bsdf_node, {"reflectance"}, 0.5f);
        uint64_t id_roughness = ReadTexture(bsdf_node, {"alpha"}, 0.2f);
        info.type = BsdfType::kRoughDiffuse;
        info.rough_diffuse.use_fast_approx = use_fast_aaprox;
        info.rough_diffuse.id_diffuse_reflectance = id_reflectance;
        info.rough_diffuse.id_roughness = id_roughness;
        break;
    }
    case "thindielectric"_hash:
    {
        is_thin_dielectric = true;
    }
    case "dielectric"_hash:
    case "roughdielectric"_hash:
    {
        info.twosided = true;
        const float int_ior =
            ReadDielectricIor(bsdf_node, {"int_ior", "intIOR"}, 1.5046f);
        const float ext_ior =
            ReadDielectricIor(bsdf_node, {"ext_ior", "extIOR"}, 1.000277f);
        uint64_t id_roughness_u, id_roughness_v;
        if (type == "roughdielectric")
        {
            if (basic_parser::GetChildNodeByName(bsdf_node, {"alpha"}, nullptr))
            {
                id_roughness_u = ReadTexture(bsdf_node, {"alpha"}, 0.1f);
                id_roughness_v = id_roughness_u;
            }
            else
            {
                id_roughness_u =
                    ReadTexture(bsdf_node, {"alpha_u", "alphaU"}, 0.1f);
                id_roughness_v =
                    ReadTexture(bsdf_node, {"alpha_v", "alphaV"}, 0.1f);
            }
        }
        else
        {
            id_roughness_u =
                ReadTexture(bsdf_node, std::vector<std::string>{}, 0.001f);
            id_roughness_v = id_roughness_u;
        }
        const uint64_t id_specular_reflectance = ReadTexture(
            bsdf_node, {"specularReflectance", "specular_reflectance"}, 1.0f);
        const uint64_t id_specular_transmittance = ReadTexture(
            bsdf_node, {"specularTransmittance", "specular_transmittance"},
            1.0f);
        info.type = is_thin_dielectric ? BsdfType::kThinDielectric
                                       : BsdfType::kDielectric;
        info.dielectric.eta = int_ior / ext_ior;
        info.dielectric.id_roughness_u = id_roughness_u;
        info.dielectric.id_roughness_v = id_roughness_v;
        info.dielectric.id_specular_reflectance = id_specular_reflectance;
        info.dielectric.id_specular_transmittance = id_specular_transmittance;
        break;
    }
    case "conductor"_hash:
    case "roughconductor"_hash:
    {
        uint64_t id_roughness_u, id_roughness_v;
        if (type == "roughconductor")
        {
            if (basic_parser::GetChildNodeByName(bsdf_node, {"alpha"}, nullptr))
            {
                id_roughness_u = ReadTexture(bsdf_node, {"alpha"}, 0.1f);
                id_roughness_v = id_roughness_u;
            }
            else
            {
                id_roughness_u =
                    ReadTexture(bsdf_node, {"alpha_u", "alphaU"}, 0.1f);
                id_roughness_v =
                    ReadTexture(bsdf_node, {"alpha_v", "alphaV"}, 0.1f);
            }
        }
        else
        {
            id_roughness_u =
                ReadTexture(bsdf_node, std::vector<std::string>{}, 0.001f);
            id_roughness_v = id_roughness_u;
        }

        const uint64_t id_specular_reflectance = ReadTexture(
            bsdf_node, {"specularReflectance", "specular_reflectance"}, 1.0f);
        Vec3 eta, k;
        ReadConductorIor(bsdf_node, &eta, &k);
        const Vec3 reflectivity =
            (Sqr(eta - 1.0f) + Sqr(k)) / (Sqr(eta + 1.0f) + Sqr(k));
        const Vec3 temp1 = 1.0f + Sqrt(reflectivity),
                   temp2 = 1.0f - Sqrt(reflectivity),
                   temp3 = ((1.0f - reflectivity) / (1.0 + reflectivity));
        const Vec3 edgetint = (temp1 - eta * temp2) / (temp1 - temp3 * temp2);

        info.type = BsdfType::kConductor;
        info.conductor.id_roughness_u = id_roughness_u;
        info.conductor.id_roughness_v = id_roughness_v;
        info.conductor.id_specular_reflectance = id_specular_reflectance;
        info.conductor.reflectivity = reflectivity;
        info.conductor.edgetint = edgetint;
        break;
    }
    case "plastic"_hash:
    case "roughplastic"_hash:
    {
        const float int_ior =
            ReadDielectricIor(bsdf_node, {"int_ior", "intIOR"}, 1.5046f);
        const float ext_ior =
            ReadDielectricIor(bsdf_node, {"ext_ior", "extIOR"}, 1.000277f);
        uint64_t id_roughness;
        if (type == "roughplastic")
            id_roughness = ReadTexture(bsdf_node, {"alpha"}, 0.1f);
        else
            id_roughness =
                ReadTexture(bsdf_node, std::vector<std::string>{}, 0.001f);
        const uint64_t id_diffuse_reflectance = ReadTexture(
            bsdf_node, {"diffuseReflectance", "diffuse_reflectance"}, 1.0f);
        const uint64_t id_specular_reflectance = ReadTexture(
            bsdf_node, {"specularReflectance", "specular_reflectance"}, 1.0f);

        info.type = BsdfType::kPlastic;
        info.plastic.eta = int_ior / ext_ior;
        info.plastic.id_roughness = id_roughness;
        info.plastic.id_diffuse_reflectance = id_diffuse_reflectance;
        info.plastic.id_specular_reflectance = id_specular_reflectance;
        break;
    }
    default:
    {
        fprintf(stderr,
                "[warning] unsupport bsdf type '%s', use default 'diffuse' "
                "instead.\n",
                type.c_str());
        const uint64_t id_reflectance =
            ReadTexture(bsdf_node, std::vector<std::string>{}, 0.5f);
        info.type = BsdfType::kDiffuse;
        info.diffuse.id_diffuse_reflectance = id_reflectance;
        break;
    }
    }
    local::config.bsdfs.push_back(info);
    local::map_bsdf[id] = id_bsdf;
    return id_bsdf;
}

float element_parser::ReadDielectricIor(
    const pugi::xml_node &parent_node,
    const std::vector<std::string> &valid_names, float defalut_value)
{
    pugi::xml_node ior_node;
    basic_parser::GetChildNodeByName(parent_node, valid_names, &ior_node);
    if (ior_node.name() == std::string("string"))
    {
        std::string material_name = ior_node.attribute("value").as_string();
        float ior = 0.0;
        if (!ior_lut::LookupDielectricIor(material_name, &ior))
        {
            std::ostringstream oss;
            oss << "unsupported  material'" << material_name << "'.";
            throw MyException(oss.str());
        }
        return ior;
    }
    else
    {
        return ior_node.attribute("value").as_float(defalut_value);
    }
}

void element_parser::ReadConductorIor(const pugi::xml_node &parent_node,
                                      Vec3 *eta, Vec3 *k)
{
    pugi::xml_node child_node;
    if (basic_parser::GetChildNodeByName(parent_node, {"material"},
                                         &child_node))
    {
        std::string material_name = child_node.attribute("value").as_string();
        if (!ior_lut::LookupConductorIor(material_name, eta, k))
        {
            std::ostringstream oss;
            oss << "unsupported  material'" << material_name << "'.";
            throw MyException(oss.str());
        }
    }
    else if (basic_parser::GetChildNodeByName(parent_node, {"eta"},
                                              &child_node))
    {
        *eta = basic_parser::ReadVec3(child_node, Vec3{1.0, 1.0, 1.0}, "");
        if (!basic_parser::GetChildNodeByName(parent_node, {"k"}, &child_node))
        {
            std::ostringstream oss;
            oss << "cannot find 'k for Conductor bsdf'"
                << parent_node.attribute("id").as_string() << "'.";
            throw MyException(oss.str());
        }
        *k = basic_parser::ReadVec3(child_node, Vec3{1.0, 1.0, 1.0}, "");
    }
    else
    {
        ior_lut::LookupConductorIor("Cu", eta, k);
    }
}

uint64_t element_parser::ReadShape(const pugi::xml_node &shape_node)
{
    std::string id = shape_node.attribute("id").value();
    const uint32_t index = local::config.instances.size();
    if (id.empty())
        id = "shape_" + std::to_string(index);

    uint32_t id_bsdf = kInvalidId;
    if (shape_node.child("emitter"))
    {
        pugi::xml_node emitter_node = shape_node.child("emitter");
        if (!emitter_node.child("rgb"))
        {
            std::ostringstream oss;
            oss << "cannot find radiance for area light '" << id << "'.";
            throw MyException(oss.str());
        }

        const Vec3 radiance =
            basic_parser::ReadVec3(emitter_node, {"radiance"}, Vec3(1));
        const uint32_t index_texture = local::config.textures.size();
        const std::string texture_id =
            "texture_" + std::to_string(index_texture);
        local::map_texture[texture_id] = index_texture;
        TextureInfo info_texture;
        info_texture.type = TextureType::kConstant;
        info_texture.constant.color = radiance;
        local::config.textures.push_back(info_texture);

        size_t id_radiance = local::map_texture[texture_id];
        id_bsdf = local::config.bsdfs.size();
        BsdfInfo info_bsdf;
        info_bsdf.type = BsdfType::kAreaLight;
        info_bsdf.area_light.id_radiance = id_radiance;
        info_bsdf.area_light.weight = 1;
        info_bsdf.twosided = false;
        info_bsdf.id_opacity = kInvalidId;
        info_bsdf.id_bump_map = kInvalidId;
        local::config.bsdfs.push_back(info_bsdf);
        local::map_bsdf[id] = id_bsdf;
    }
    else if (shape_node.child("bsdf"))
    {
        id_bsdf = ReadBsdf(shape_node.child("bsdf"), "", kInvalidId, kInvalidId,
                           false);
    }
    else
    {
        for (pugi::xml_node ref_node : shape_node.children("ref"))
        {
            std::string bsdf_id =
                shape_node.child("ref").attribute("id").value();
            if (local::map_bsdf.count(bsdf_id))
            {
                id_bsdf = local::map_bsdf.at(bsdf_id);
                break;
            }
        }
    }

    InstanceInfo info;
    info.id_bsdf = id_bsdf;
    info.flip_normals = basic_parser::ReadBoolean(
        shape_node, {"flip_normals", "flipNormals"}, false);
    info.to_world = basic_parser::ReadTransform4(shape_node.child("transform"));

    std::string type = shape_node.attribute("type").value();
    switch (Hash(type.c_str()))
    {
    case "cube"_hash:
    {
        info.type = InstanceType::kCube;
        break;
    }
    case "rectangle"_hash:
    {
        info.type = InstanceType::kRectangle;
        break;
    }
    case "sphere"_hash:
    {
        info.type = InstanceType::kSphere;
        info.sphere.radius =
            shape_node.child("float").attribute("value").as_float(1.0);
        info.sphere.center =
            basic_parser::ReadVec3(shape_node, {"center"}, Vec3(0));
        break;
    }
    case "disk"_hash:
    {
        info.type = InstanceType::kDisk;
        break;
    }
    case "cylinder"_hash:
    {
        info.type = InstanceType::kCylinder;
        info.cylinder.p0 = basic_parser::ReadVec3(shape_node, {"p0"}, Vec3(0));
        info.cylinder.p1 =
            basic_parser::ReadVec3(shape_node, {"p1"}, Vec3(0, 0, 1));
        info.cylinder.radius =
            shape_node.child("float").attribute("value").as_float(1.0f);
        break;
    }
    case "obj"_hash:
    case "serialized"_hash:
    case "gltf"_hash:
    case "ply"_hash:
    {
        info.type = InstanceType::kMeshes;
        std::string filename =
            local::current_directory +
            shape_node.child("string").attribute("value").as_string();
        bool face_normals = basic_parser::ReadBoolean(
            shape_node, {"face_normals", "faceNormals"}, false);
        if (type == "obj")
        {
            bool flip_texcoords = basic_parser::ReadBoolean(
                shape_node, {"flip_tex_coords", "flipTexCoords"}, true);
            info.meshes =
                model_loader::Load(filename, flip_texcoords, face_normals);
        }
        else if (type == "serialized")
        {
            int index_shape =
                shape_node.child("integer").attribute("value").as_int(0);
            info.meshes =
                model_loader::Load(filename, index_shape, false, face_normals);
        }
        else
        {
            info.meshes = model_loader::Load(filename, false, face_normals);
        }
        break;
    }
    default:
    {
        fprintf(stderr, "[warning] unsupported shape type '%s', ignore it.\n",
                type.c_str());
        break;
    }
    }

    pugi::xml_node medium_int_node;
    if (basic_parser::GetChildNodeByName(shape_node, {"interior"},
                                         &medium_int_node))
    {
        info.id_medium_int = ReadMedium(medium_int_node);
    }

    pugi::xml_node medium_ext_node;
    if (basic_parser::GetChildNodeByName(shape_node, {"exterior"},
                                         &medium_ext_node))
    {
        info.id_medium_ext = ReadMedium(medium_ext_node);
    }

    local::config.instances.push_back(info);

    return 0;
}

void element_parser::ReadEmitter(const pugi::xml_node &emitter_node)
{
    std::string type = emitter_node.attribute("type").as_string();
    switch (Hash(type.c_str()))
    {
    case "point"_hash:
    {
        EmitterInfo info;
        info.type = EmitterType::kPoint;
        info.point.position =
            basic_parser::ReadVec3(emitter_node, {"position"}, Vec3(0));
        const Mat4 to_world =
            basic_parser::ReadTransform4(emitter_node.child("transform"));
        info.point.position = TransformPoint(to_world, info.point.position);
        info.point.intensity =
            basic_parser::ReadVec3(emitter_node, {"intensity"}, Vec3(1));
        local::config.emitters.push_back(info);
        break;
    }
    case "spot"_hash:
    {
        EmitterInfo info;
        info.type = EmitterType::kSpot;
        info.spot.intensity =
            basic_parser::ReadVec3(emitter_node, {"intensity"}, Vec3(1));
        info.spot.to_world =
            basic_parser::ReadTransform4(emitter_node.child("transform"));
        info.spot.cutoff_angle = basic_parser::ReadFloat(
            emitter_node, {"cutoff_angle", "cutoffAngle"}, 20);
        info.spot.beam_width =
            basic_parser::ReadFloat(emitter_node, {"beamWidth", "beam_width"},
                                    info.spot.cutoff_angle * 0.75f);
        info.spot.cutoff_angle = ToRadians(info.spot.cutoff_angle),
        info.spot.beam_width = ToRadians(info.spot.beam_width);
        info.spot.id_texture = kInvalidId;
        if (emitter_node.child("texture"))
        {
            info.spot.id_texture =
                ReadTexture(emitter_node.child("texture"), 1.0f, 1.0f);
        }
        local::config.emitters.push_back(info);
        break;
    }
    case "directional"_hash:
    {
        EmitterInfo info;
        info.type = EmitterType::kDirectional;
        const Mat4 to_world =
            basic_parser::ReadTransform4(emitter_node.child("transform"));
        const Vec3 dir_local =
            basic_parser::ReadVec3(emitter_node, {"direction"}, Vec3{0, 0, 1});
        info.directional.direction =
            TransformVector(to_world.Transpose().Inverse(), dir_local);
        info.directional.radiance = basic_parser::ReadVec3(
            emitter_node, {"radiance", "irradiance"}, Vec3(1));
        local::config.emitters.push_back(info);
        break;
    }
    case "sun"_hash:
    case "sky"_hash:
    case "sunsky"_hash:
    {
        int resolution =
            basic_parser::ReadInt(emitter_node, {"resolution"}, 512);
        int width = resolution, height = resolution / 2;

        Vec3 sun_direction(0);
        pugi::xml_node sun_direction_node;
        if (basic_parser::GetChildNodeByName(emitter_node, {"sunDirection"},
                                             &sun_direction_node))
        {
            sun_direction = {sun_direction_node.attribute("x").as_float(),
                             sun_direction_node.attribute("y").as_float(),
                             sun_direction_node.attribute("z").as_float()};
        }
        else
        {
            LocationDate location_date;
            location_date.year =
                basic_parser::ReadInt(emitter_node, {"year"}, 2010);
            location_date.month =
                basic_parser::ReadInt(emitter_node, {"month"}, 7);
            location_date.day =
                basic_parser::ReadInt(emitter_node, {"day"}, 10);
            location_date.hour =
                basic_parser::ReadFloat(emitter_node, {"hour"}, 15);
            location_date.minute =
                basic_parser::ReadFloat(emitter_node, {"minute"}, 0);
            location_date.second =
                basic_parser::ReadFloat(emitter_node, {"second"}, 0);
            location_date.latitude =
                basic_parser::ReadFloat(emitter_node, {"latitude"}, 35.6894);
            location_date.longitude =
                basic_parser::ReadFloat(emitter_node, {"longitude"}, 139.6917);
            location_date.timezone =
                basic_parser::ReadFloat(emitter_node, {"timezone"}, 9);
            sun_direction = GetSunDirection(location_date);
        }

        float turbidity =
            basic_parser::ReadFloat(emitter_node, {"turbidity"}, 3);
        turbidity = fminf(fmaxf(turbidity, 10.0f), 1.0f);

        if (type == "sun" || type == "sunsky")
        {
            double sun_scale =
                basic_parser::ReadFloat(emitter_node, {"sunScale"}, 1);
            double sun_radius_scale =
                basic_parser::ReadFloat(emitter_node, {"sunRadiusScale"}, 1);

            Vec3 sun_radiance;
            std::vector<float> data = CreateSunTexture(
                sun_direction, turbidity, sun_scale, sun_radius_scale, width,
                height, &sun_radiance);

            const uint64_t id_texture = local::config.textures.size();
            local::map_texture["sun_texture"] = id_texture;
            TextureInfo info_texture;
            info_texture.type = TextureType::kBitmap;
            info_texture.bitmap.data = data;
            info_texture.bitmap.width = width;
            info_texture.bitmap.height = height;
            info_texture.bitmap.channel = 3;
            info_texture.bitmap.to_uv = {};
            local::config.textures.push_back(info_texture);

            EmitterInfo info_emitter;
            info_emitter.type = EmitterType::kSun;
            info_emitter.sun.direction = -sun_direction;
            info_emitter.sun.radiance = sun_radiance;
            info_emitter.sun.cos_cutoff_angle =
                cosf(ToRadians(kSunAppRadius * 0.5f) * sun_radius_scale);
            info_emitter.sun.id_texture = id_texture;

            local::config.emitters.push_back(info_emitter);
        }

        if (type == "sky" || type == "sunsky")
        {
            Vec3 albedo =
                basic_parser::ReadVec3(emitter_node, {"albedo"}, Vec3(0.15f));

            float stretch =
                basic_parser::ReadFloat(emitter_node, {"stretch"}, 1);
            stretch = std::min(std::max(turbidity, 2.0f), 1.0f);

            float sky_scale =
                basic_parser::ReadFloat(emitter_node, {"skyScale"}, 1);
            float extend =
                basic_parser::ReadBoolean(emitter_node, {"extend"}, true);

            std::vector<float> data =
                CreateSkyTexture(sun_direction, albedo, turbidity, stretch,
                                 sky_scale, extend, width, height);

            const uint64_t id_texture = local::config.textures.size();
            local::map_texture["sky_texture"] = id_texture;
            TextureInfo info_texture;
            info_texture.type = TextureType::kBitmap;
            info_texture.bitmap.data = data;
            info_texture.bitmap.width = width;
            info_texture.bitmap.height = height;
            info_texture.bitmap.channel = 3;
            info_texture.bitmap.to_uv = {};
            local::config.textures.push_back(info_texture);

            EmitterInfo info;
            info.type = EmitterType::kEnvMap;
            info.envmap.id_radiance = id_texture;
            info.envmap.to_world = {};
            local::config.emitters.push_back(info);
        }
        break;
    }
    case "envmap"_hash:
    {
        const std::string filename =
            emitter_node.child("string").attribute("value").as_string();
        const float gamma =
            basic_parser::ReadFloat(emitter_node, {"gamma"}, 0.0f);
        const float scale =
            basic_parser::ReadFloat(emitter_node, {"scale"}, 1.0f);
        int width_target = static_cast<int>(local::config.camera.width * 360 /
                                            local::config.camera.fov_x);
        EmitterInfo info;
        info.type = EmitterType::kEnvMap;
        info.envmap.id_radiance =
            ReadBitmap(local::current_directory + filename, filename, gamma,
                       scale, &width_target);
        info.envmap.to_world =
            basic_parser::ReadTransform4(emitter_node.child("transform"));
        local::config.emitters.push_back(info);
        break;
    }
    case "constant"_hash:
    {
        EmitterInfo info;
        info.type = EmitterType::kConstant;
        info.constant.radiance =
            basic_parser::ReadVec3(emitter_node, {"radiance"}, Vec3(1));
        local::config.emitters.push_back(info);
        break;
    }
    default:
    {
        fprintf(stderr, "[warning] unsupport emitter '%s', ignore it.\n",
                type.c_str());
        break;
    }
    }
}

bool basic_parser::GetChildNodeByName(
    const pugi::xml_node &parent_node,
    const std::vector<std::string> &valid_names, pugi::xml_node *child_node)
{
    for (const std::string &name : valid_names)
    {
        for (pugi::xml_node node : parent_node.children())
        {
            if (node.attribute("name").value() == name)
            {
                if (child_node != nullptr)
                    *child_node = node;
                return true;
            }
        }
    }
    return false;
}

bool basic_parser::ReadBoolean(const pugi::xml_node &parent_node,
                               const std::vector<std::string> &valid_names,
                               const bool defalut_value)
{
    pugi::xml_node target_node;
    GetChildNodeByName(parent_node, valid_names, &target_node);
    return target_node.attribute("value").as_bool(defalut_value);
}

int basic_parser::ReadInt(const pugi::xml_node &parent_node,
                          const std::vector<std::string> &valid_names,
                          const int defalut_value)
{
    pugi::xml_node target_node;
    GetChildNodeByName(parent_node, valid_names, &target_node);
    return target_node.attribute("value").as_int(defalut_value);
}

float basic_parser::ReadFloat(const pugi::xml_node &parent_node,
                              const std::vector<std::string> &valid_names,
                              const float defalut_value)
{
    pugi::xml_node target_node;
    GetChildNodeByName(parent_node, valid_names, &target_node);
    return target_node.attribute("value").as_float(defalut_value);
}

Vec3 basic_parser::ReadVec3(const pugi::xml_node &parent_node,
                            const std::vector<std::string> &valid_names,
                            const Vec3 &defalut_value)
{
    pugi::xml_node target_node;
    GetChildNodeByName(parent_node, valid_names, &target_node);
    return target_node ? ReadVec3(target_node, defalut_value, "")
                       : defalut_value;
}

Vec3 basic_parser::ReadVec3(const pugi::xml_node &vec3_node,
                            const Vec3 &defalut_value, std::string value_name)
{
    if (!vec3_node.attribute("value") && value_name.empty())
    {
        return {vec3_node.attribute("x").as_float(defalut_value.x),
                vec3_node.attribute("y").as_float(defalut_value.y),
                vec3_node.attribute("z").as_float(defalut_value.z)};
    }

    if (value_name.empty())
        value_name = "value";
    std::string str_buffer =
        vec3_node.attribute(value_name.c_str()).as_string();

    const int num_space =
        static_cast<int>(std::count(str_buffer.begin(), str_buffer.end(), ' '));
    if (num_space == 0)
    {
        const float value =
            vec3_node.attribute("value").as_float(defalut_value.x);
        return Vec3(value);
    }
    else if (num_space == 2)
    {
        Vec3 result(0);
        const int num_comma = static_cast<int>(
            std::count(str_buffer.begin(), str_buffer.end(), ','));
        if (num_comma == 0)
        {
            sscanf(str_buffer.c_str(), "%f %f %f", &result[0], &result[1],
                   &result[2]);
            return result;
        }
        else
        {
            sscanf(str_buffer.c_str(), "%f, %f, %f", &result[0], &result[1],
                   &result[2]);
            return result;
        }
    }
    else
    {
        return defalut_value;
    }
}

Mat4 basic_parser::ReadMat4(const pugi::xml_node &matrix_node)
{
    if (!matrix_node.attribute("value"))
        return Mat4();

    Mat4 result;
    std::string str_buffer = matrix_node.attribute("value").as_string();
    const int space_count =
        static_cast<int>(std::count(str_buffer.begin(), str_buffer.end(), ' '));
    if (space_count == 8)
    {
        sscanf(str_buffer.c_str(), "%f %f %f %f %f %f %f %f %f", &result[0][0],
               &result[0][0], &result[0][1], &result[1][0], &result[1][1],
               &result[1][2], &result[2][0], &result[2][1], &result[2][2]);
    }
    else
    {
        sscanf(str_buffer.c_str(),
               "%f %f %f %f %f %f %f %f %f %f %f %f %f %f %f %f", &result[0][0],
               &result[0][1], &result[0][2], &result[0][3], &result[1][0],
               &result[1][1], &result[1][2], &result[1][3], &result[2][0],
               &result[2][1], &result[2][2], &result[2][3], &result[3][0],
               &result[3][1], &result[3][2], &result[3][3]);
    }
    return result;
}

Mat4 basic_parser::ReadTransform4(const pugi::xml_node &transform_node)
{
    Mat4 result;
    if (!transform_node)
        return result;

    for (pugi::xml_node node : transform_node.children())
    {
        const char *name = node.name();
        switch (Hash(name))
        {
        case "translate"_hash:
        {
            Vec3 translate = ReadVec3(node, Vec3(0), "");
            result = Mul(Translate(translate), result);
            break;
        }
        case "rotate"_hash:
        {
            Vec3 axis = ReadVec3(node, Vec3(0), "");
            float angle = node.attribute("angle").as_float(0);
            result = Mul(Rotate(ToRadians(angle), axis), result);
            break;
        }
        case "scale"_hash:
        {
            Vec3 scale = ReadVec3(node, Vec3(1), "");
            result = Mul(Scale(scale), result);
            break;
        }
        case "matrix"_hash:
        {
            Mat4 matrix = ReadMat4(node);
            result = Mul(matrix, result);
            break;
        }
        case "lookat"_hash:
        {
            Vec3 origin = ReadVec3(node, Vec3{0, 0, 0}, "origin"),
                 target = ReadVec3(node, Vec3{1, 0, 0}, "target"),
                 up = ReadVec3(node, Vec3{0, 1, 0}, "up");
            result = Mul(LookAtLH(origin, target, up).Inverse(), result);
            break;
        }
        default:
        {
            fprintf(stderr,
                    "[warning] unsupport transform type '%s', ignore it.\n",
                    name);
            break;
        }
        }
    }
    return result;
}
