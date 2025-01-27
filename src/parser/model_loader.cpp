#include "csrt/parser/model_loader.hpp"

#include <algorithm>
#include <assimp/Importer.hpp>
#include <assimp/postprocess.h>
#include <cstdio>
#include <exception>
#include <sstream>
#include <thread>
#include <zlib.h>

namespace
{

using namespace csrt;

constexpr size_t kZstreamBufferSize = 32768;

std::string g_filename;
FILE *g_stream;
uint8_t g_inflateBuffer[kZstreamBufferSize];
z_stream g_deflateStream;
z_stream g_inflateStream;

void InitZlib()
{
    g_deflateStream.zalloc = Z_NULL;
    g_deflateStream.zfree = Z_NULL;
    g_deflateStream.opaque = Z_NULL;
    int retval = deflateInit2(&g_deflateStream, Z_DEFAULT_COMPRESSION,
                              Z_DEFLATED, 15, 8, Z_DEFAULT_STRATEGY);
    if (retval != Z_OK)
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "cannot initialize ZLIB: error code '" << retval << "'.";
        throw MyException(oss.str());
    }

    g_inflateStream.zalloc = Z_NULL;
    g_inflateStream.zfree = Z_NULL;
    g_inflateStream.opaque = Z_NULL;
    g_inflateStream.avail_in = 0;
    g_inflateStream.next_in = Z_NULL;
    retval = inflateInit2(&g_inflateStream, 15);
    if (retval != Z_OK)
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "cannot initialize ZLIB: error code '" << retval << "'.";
        throw MyException(oss.str());
    }
}

bool IsNotLittleEndian()
{
    union
    {
        uint8_t charValue[2];
        uint16_t shortValue;
    };

    charValue[0] = 1;
    charValue[1] = 0;

    return shortValue != 1;
}
const bool kNotLittleEndian = IsNotLittleEndian();

template <typename T>
T SwapEndianness(T value)
{
    union
    {
        T value;
        uint8_t byteValue[sizeof(T)];
    } u;

    u.value = value;
    std::reverse(&u.byteValue[0], &u.byteValue[sizeof(T)]);
    return u.value;
}

void Seek(size_t pos)
{
#ifdef WIN32
    if (_fseeki64(g_stream, pos, SEEK_SET))
#else
    if (fseeko(g_stream, (off_t)pos, SEEK_SET))
#endif
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "error while trying to seek to position '" << pos
            << "' in file '" << g_filename << "'.";
        throw MyException(oss.str());
    }
}

size_t GetPos()
{
#ifdef WIN32
    long long pos = _ftelli64(g_stream);
#else
    off_t pos = ftello(g_stream);
#endif
    if (pos == -1)
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "error while looking up the position in file '" << g_filename
            << "'.";
        throw MyException(oss.str());
    }
    return static_cast<size_t>(pos);
}

size_t GetSize()
{
    size_t tmp = GetPos();
    if (fseek(g_stream, 0, SEEK_END))
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "error while seeking within file '" << g_filename << "'.";
        throw MyException(oss.str());
    }

    size_t size = GetPos();
#ifdef WIN32
    if (_fseeki64(g_stream, tmp, SEEK_SET))
#else
    if (fseeko(g_stream, tmp, SEEK_SET))
#endif
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "error while seeking within file '" << g_filename << "'.";
        throw MyException(oss.str());
    }

    return size;
}

void ReadFile(void *ptr, size_t size)
{
    size_t bytesRead = fread(ptr, 1, size, g_stream);
    if (bytesRead != size)
    {
        fclose(g_stream);
        std::ostringstream oss;
        if (ferror(g_stream) != 0)
            oss << "error while reading from file '" << g_filename << "'.";
        else
            oss << "read less data than expected from file '" << g_filename
                << "'.";
        throw MyException(oss.str());
    }
}

void ReadCompressedFile(void *ptr, size_t size)
{
    uint8_t *targetPtr = (uint8_t *)ptr;
    while (size > 0)
    {
        if (g_inflateStream.avail_in == 0)
        {
            size_t remaining = GetSize() - GetPos();
            g_inflateStream.next_in = g_inflateBuffer;
            g_inflateStream.avail_in =
                (uInt)std::min(remaining, sizeof(g_inflateBuffer));
            if (g_inflateStream.avail_in == 0)
            {
                fclose(g_stream);
                std::ostringstream oss;
                oss << "read less data than expected from file '" << g_filename
                    << "'.";
                throw MyException(oss.str());
            }

            ReadFile(g_inflateBuffer, g_inflateStream.avail_in);
        }

        g_inflateStream.avail_out = (uInt)size;
        g_inflateStream.next_out = targetPtr;

        int retval = inflate(&g_inflateStream, Z_NO_FLUSH);
        switch (retval)
        {
        case Z_STREAM_ERROR:
        {
            fclose(g_stream);
            std::ostringstream oss;
            oss << "inflate(): stream error for file '" << g_filename << "'.";
            throw MyException(oss.str());
            break;
        }
        case Z_NEED_DICT:
        {
            fclose(g_stream);
            std::ostringstream oss;
            oss << "inflate(): need dictionary for file '" << g_filename
                << "'.";
            throw MyException(oss.str());
            break;
        }
        case Z_DATA_ERROR:
        {
            fclose(g_stream);
            std::ostringstream oss;
            oss << "inflate(): data error for file '" << g_filename << "'.";
            throw MyException(oss.str());
            break;
        }
        case Z_MEM_ERROR:
        {
            fclose(g_stream);
            std::ostringstream oss;
            oss << "inflate(): memory error for file '" << g_filename << "'.";
            throw MyException(oss.str());
            break;
        }
        };

        size_t outputSize = size - (size_t)g_inflateStream.avail_out;
        targetPtr += outputSize;
        size -= outputSize;

        if (size > 0 && retval == Z_STREAM_END)
        {
            fclose(g_stream);
            std::ostringstream oss;
            oss << "inflate(): attempting to read past the end of the "
                   "stream for file '"
                << g_filename << "'.";
            throw MyException(oss.str());
        }
    }
}

std::string ReadCompressedString()
{
    std::string retval;
    char data;
    do
    {
        ReadCompressedFile(&data, sizeof(char));
        if (data != 0)
            retval += data;
    } while (data != 0);
    return retval;
}

template <typename T>
T ReadElement(bool compressed)
{
    T value;
    if (compressed)
        ReadCompressedFile(&value, sizeof(T));
    else
        ReadFile(&value, sizeof(T));
    if (kNotLittleEndian)
        value = SwapEndianness(value);

    return value;
}

template <typename T>
void ReadCompressedArray(T *data, size_t size)
{
    ReadCompressedFile(data, sizeof(T) * size);
    if (kNotLittleEndian)
    {
        for (size_t i = 0; i < size; ++i)
            data[i] = SwapEndianness(data[i]);
    }
}

template <typename VecType, int vec_size, typename ElementType,
          typename RawType>
std::vector<VecType> ReadVectorArray(size_t array_size)
{
    std::vector<VecType> data(array_size);
    const size_t num_emelent = array_size * vec_size;
    std::vector<RawType> raw_data(num_emelent);
    ReadCompressedArray<RawType>(raw_data.data(), num_emelent);

    for (size_t i = 0; i < array_size; ++i)
    {
        for (int j = 0; j < vec_size; ++j)
            data[i][j] = static_cast<ElementType>(raw_data[i * vec_size + j]);
    }
    return data;
}

void ProcessOffset(int index_shape, uint16_t version)
{
    if (index_shape == 0)
        return;

    size_t offset = 0;
    const size_t stream_size = GetSize();

    /* Determine the position of the requested substream. This is stored
        at the end of the file */
    Seek(stream_size - sizeof(uint32_t));

    uint32_t count = ReadElement<uint32_t>(false);
    if (index_shape < 0 || index_shape > static_cast<int>(count))
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "unable to unserialize mesh, shape index is out of range "
               "for file '"
            << g_filename << "'.";
        throw MyException(oss.str());
    }

    if (version == 0x0004)
    {
        Seek(GetSize() - sizeof(uint64_t) * (count - index_shape) -
             sizeof(uint32_t));
        offset = ReadElement<uint64_t>(false);
    }
    else
    {
        Seek(GetSize() - sizeof(uint32_t) * (count - index_shape + 1));
        offset = ReadElement<uint32_t>(false);
    }

    Seek(offset);
    Seek(GetPos() + sizeof(uint16_t) * 2);
}

MeshesInfo ProcessAssimpNode(const aiScene *scene, aiNode *node,
                             bool face_normals, const uint32_t index_offset)
{
    MeshesInfo info_meshes;
    for (unsigned int i = 0; i < node->mNumMeshes; ++i)
    {
        aiMesh *mesh = scene->mMeshes[node->mMeshes[i]];
        for (unsigned int j = 0; j < mesh->mNumFaces; ++j)
        {
            const aiFace face = mesh->mFaces[j];
            info_meshes.indices.push_back({index_offset + face.mIndices[0],
                                           index_offset + face.mIndices[1],
                                           index_offset + face.mIndices[2]});
        }
        if (mesh->mTextureCoords[0])
        {
            for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
            {
                info_meshes.texcoords.push_back({mesh->mTextureCoords[0][j].x,
                                                 mesh->mTextureCoords[0][j].y});
            }
        }
        for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
        {
            info_meshes.positions.push_back({mesh->mVertices[j].x,
                                             mesh->mVertices[j].y,
                                             mesh->mVertices[j].z});
        }
        if (!face_normals)
        {
            for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
            {
                info_meshes.normals.push_back({mesh->mNormals[j].x,
                                               mesh->mNormals[j].y,
                                               mesh->mNormals[j].z});
            }
        }
        if (mesh->mTangents)
        {
            for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
            {
                info_meshes.tangents.push_back({mesh->mTangents[j].x,
                                                mesh->mTangents[j].y,
                                                mesh->mTangents[j].z});
            }
        }
        if (mesh->mBitangents)
        {
            for (unsigned int j = 0; j < mesh->mNumVertices; ++j)
            {
                info_meshes.bitangents.push_back({mesh->mBitangents[j].x,
                                                  mesh->mBitangents[j].y,
                                                  mesh->mBitangents[j].z});
            }
        }
    }

    for (unsigned int i = 0; i < node->mNumChildren; ++i)
    {
        MeshesInfo info_meshes_local =
            ProcessAssimpNode(scene, node->mChildren[i], face_normals,
                              info_meshes.indices.size());

        info_meshes.indices.insert(info_meshes.indices.end(),
                                   info_meshes_local.indices.begin(),
                                   info_meshes_local.indices.end());
        info_meshes.texcoords.insert(info_meshes.texcoords.end(),
                                     info_meshes_local.texcoords.begin(),
                                     info_meshes_local.texcoords.end());
        info_meshes.positions.insert(info_meshes.positions.end(),
                                     info_meshes_local.positions.begin(),
                                     info_meshes_local.positions.end());
        info_meshes.normals.insert(info_meshes.normals.end(),
                                   info_meshes_local.normals.begin(),
                                   info_meshes_local.normals.end());
        info_meshes.tangents.insert(info_meshes.tangents.end(),
                                    info_meshes_local.tangents.begin(),
                                    info_meshes_local.tangents.end());
        info_meshes.bitangents.insert(info_meshes.bitangents.end(),
                                      info_meshes_local.bitangents.begin(),
                                      info_meshes_local.bitangents.end());
    }

    return info_meshes;
}

} // namespace

namespace csrt
{

MeshesInfo model_loader::Load(const std::string &filename, int index_shape,
                              bool flip_texcoords, bool face_normals)
{
    g_filename = filename;
    g_stream = fopen(filename.c_str(), "rb");
    if (g_stream == nullptr)
    {
        std::ostringstream oss;
        oss << "read file '" << filename << "' failed.";
        throw MyException(oss.str());
    }

    uint16_t format = ReadElement<uint16_t>(false);
    if (format != 0x041C)
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "invalid file format for '" << filename << "'.";
        throw MyException(oss.str());
    }

    uint16_t version = ReadElement<uint16_t>(false);
    if (version != 0x0003 && format != 0x0004)
    {
        fclose(g_stream);
        std::ostringstream oss;
        oss << "invalid file version for '" << filename << "'.";
        throw MyException(oss.str());
    }

    ProcessOffset(index_shape, version);
    InitZlib();

    uint32_t flags = ReadElement<uint32_t>(true);

    std::string name;
    if (version == 0x0004)
        name = ReadCompressedString();

    uint64_t vertex_count = ReadElement<uint64_t>(true);
    uint64_t triangle_count = ReadElement<uint64_t>(true);
    bool double_precision = flags & 0x2000;

    MeshesInfo info_meshes;

    info_meshes.positions =
        double_precision ? ReadVectorArray<Vec3, 3, float, double>(vertex_count)
                         : ReadVectorArray<Vec3, 3, float, float>(vertex_count);
    if (flags & 0x0001)
    {
        info_meshes.normals =
            double_precision
                ? ReadVectorArray<Vec3, 3, float, double>(vertex_count)
                : ReadVectorArray<Vec3, 3, float, float>(vertex_count);
    }

    if (flags & 0x0002)
    {
        info_meshes.texcoords =
            double_precision
                ? ReadVectorArray<Vec2, 2, float, double>(vertex_count)
                : ReadVectorArray<Vec2, 2, float, float>(vertex_count);
    }

    std::vector<Vec3> colors;
    if (flags & 0x0008)
    {
        colors = double_precision
                     ? ReadVectorArray<Vec3, 3, float, double>(vertex_count)
                     : ReadVectorArray<Vec3, 3, float, float>(vertex_count);
    }

    info_meshes.indices =
        ReadVectorArray<Uvec3, 3, unsigned int, uint32_t>(triangle_count);

    fclose(g_stream);

    return info_meshes;
}

MeshesInfo model_loader::Load(const std::string &filename,
                              const bool flip_texcoords,
                              const bool face_normals)
{
    fprintf(stderr, "[info] read file \"%s\"\n", filename.c_str());

    int assimp_option = aiProcess_Triangulate | aiProcess_GenUVCoords |
                        aiProcess_CalcTangentSpace;
    if (!face_normals)
        assimp_option = assimp_option | aiProcess_GenSmoothNormals;
    if (flip_texcoords)
        assimp_option = assimp_option | aiProcess_FlipUVs;

    Assimp::Importer importer;
    const aiScene *scene = importer.ReadFile(filename, assimp_option);
    if (!scene || scene->mFlags & AI_SCENE_FLAGS_INCOMPLETE ||
        !scene->mRootNode)
    {
        std::ostringstream oss;
        oss << "ASSIMP:: '" << importer.GetErrorString() << "'.";
        throw MyException(oss.str());
    }

    return ProcessAssimpNode(scene, scene->mRootNode, face_normals, 0);
}

} // namespace csrt