#include "integrator.cuh"

namespace rt
{
QUALIFIER_D_H Integrator::Integrator()
    : data_{}, size_cdf_area_light_(0), pdf_rr_rcp_(0)
{
}

QUALIFIER_D_H Integrator::Integrator(const Integrator::Data &data)
    : data_(data), size_cdf_area_light_(data.num_area_light + 1)
{
    pdf_rr_rcp_ = 1.0f / data_.pdf_rr;
}

QUALIFIER_D_H Vec3 rt::Integrator::Shade(const Vec3 &eye, const Vec3 &look_dir,
                                         uint32_t *seed) const
{
    //
    // 求取原初光线与场景的交点
    //
    Ray ray = {eye, look_dir};
    Hit hit;
    if (data_.tlas)
        hit = TraceRay(&ray, seed);
    if (!hit.valid)
    { // 原初光线逃逸出场景
        return {0};
    }
    Bsdf *bsdf = data_.bsdfs + data_.map_instance_bsdf[hit.id_instance];
    if (hit.inside && !bsdf->IsTwosided())
    { // 原初光线溯源至景物的背面，且景物的背面吸收一切光照
        return {0};
    }
    else if (bsdf->IsEmitter())
    { // 原初光线溯源至光源，不存在由 shadow ray 贡献的直接光照，返回直接光照
        return bsdf->GetRadiance(hit.texcoord);
    }

    Vec3 L(0), attenuation(1), wo = -look_dir;
    for (uint32_t depth = 1;
         depth < data_.depth_rr ||
         (depth < data_.depth_max && RandomFloat(seed) < data_.pdf_rr);
         ++depth)
    {
        // 按表面积进行抽样得到阴影光线，合并阴影光线贡献的直接光照
        L += attenuation * EvaluateDirectAreaLight(hit, wo, seed);

        // 抽样次生光线光线
        SamplingRecord rec = SampleRay(wo, hit, bsdf, RandomVec3(seed));
        if (!rec.valid)
            break;

        // 累积场景的反射率
        attenuation *= rec.attenuation / rec.pdf;
        if (fmaxf(fmaxf(attenuation.x, attenuation.y), attenuation.z) <
            kEpsilon)
            break;

        // 溯源光线
        ray = Ray(rec.position, -rec.wi);
        hit = TraceRay(&ray, seed);

        if (!hit.valid)
        { // 次生光线逃逸出场景
            break;
        }
        bsdf = data_.bsdfs + data_.map_instance_bsdf[hit.id_instance];
        if (hit.inside && !bsdf->IsTwosided())
        { // 次生光线溯源至景物的背面，且景物的背面吸收一切光照
            break;
        }
        else if (bsdf->IsEmitter())
        { // 原初光线溯源至光源，累积直接光照，并停止溯源
            const float cos_theta_prime = Dot(rec.wi, hit.normal);
            if (cos_theta_prime < kEpsilonFloat)
                break;
            const uint32_t id_instance_area_light =
                data_.map_id_instance_area_light[hit.id_instance];
            const float pdf_area =
                            (data_.cdf_area_light[id_instance_area_light + 1] -
                             data_.cdf_area_light[id_instance_area_light]) *
                            data_.list_pdf_area_instance[hit.id_instance],
                        pdf_direct =
                            pdf_area * Sqr(ray.t_max) / cos_theta_prime,
                        weight_bsdf = MisWeight(rec.pdf, pdf_direct);
            // 场景中的面光源以类似漫反射的形式向各个方向均匀地发光
            const Vec3 radiance = bsdf->GetRadiance(hit.texcoord),
                       L_dir = weight_bsdf * attenuation * radiance;
            L += L_dir;
            break;
        }
        else
        { // 原初光线溯源至不发光的景物表面，被散射
            wo = rec.wi;
            if (depth >= data_.depth_rr)
            { // 根据俄罗斯轮盘赌算法的概率处理场景的反射率，使之符合应有的数学期望
                attenuation *= pdf_rr_rcp_;
            }
        }
    }

    return L;
}

QUALIFIER_D_H Hit Integrator::TraceRay(Ray *ray, uint32_t *seed) const
{
    Hit hit;
    uint32_t id_bsdf;
    do
    {
        hit = data_.tlas->Intersect(ray);
        if (!hit.valid)
            break;
        id_bsdf = data_.map_instance_bsdf[hit.id_instance];
        if (!data_.bsdfs[id_bsdf].IsTransparent(hit.texcoord,
                                                RandomFloat(seed)))
            break;
        ray->t_min = ray->t_max + kEpsilonDistance;
        ray->t_max = kMaxFloat;
    } while (true);
    return hit;
}

QUALIFIER_D_H bool Integrator::TraceRay1(Ray *ray, uint32_t *seed) const
{
    return TraceRay(ray, seed).valid;
}

QUALIFIER_D_H Vec3 Integrator::EvaluateDirectAreaLight(const Hit &hit,
                                                       const Vec3 &wo,
                                                       uint32_t *seed) const
{
    if (data_.num_area_light == 0)
        return {0};

    // 抽样得到的面光源上一点
    const uint32_t index_area_light =
                       BinarySearch(size_cdf_area_light_, data_.cdf_area_light,
                                    RandomFloat(seed)) -
                       1,
                   id_area_light_instance =
                       data_.map_id_area_light_instance[index_area_light];
    const Hit hit_pre =
        data_.instances[id_area_light_instance].Sample(RandomVec3(seed));

    // 抽样点与当前着色点之间不能被其它物体遮挡
    const Vec3 d_vec = hit.position - hit_pre.position;
    const float distance = Length(d_vec);
    Ray ray_test = {hit_pre.position, Normalize(d_vec)};
    TraceRay1(&ray_test, seed);
    if (ray_test.t_max + kEpsilonDistance < distance)
        return {0};

    const Vec3 wi = Normalize(d_vec);
    const float cos_theta_prime = Dot(wi, hit_pre.normal);
    if (cos_theta_prime < kEpsilonFloat)
        return {0};
    if (Dot(-wi, hit.normal) < kEpsilonFloat)
        return {0};

    Bsdf *bsdf = data_.bsdfs + data_.map_instance_bsdf[hit.id_instance];
    const SamplingRecord rec = EvaluateRay(wi, wo, hit, bsdf);
    if (!rec.valid)
        return Vec3(0);

    // 根据多重重要抽样（MIS，multiple importance
    // sampling）合并按表面积进行抽样得到的阴影光线贡献的直接光照
    const float pdf_area = (data_.cdf_area_light[index_area_light + 1] -
                            data_.cdf_area_light[index_area_light]) *
                           data_.list_pdf_area_instance[id_area_light_instance],
                pdf_direct = pdf_area * Sqr(distance) / cos_theta_prime,
                weight_direct = MisWeight(pdf_direct, rec.pdf);
    Bsdf *bsdf_pre =
        data_.bsdfs + data_.map_instance_bsdf[id_area_light_instance];
    const Vec3 radiance = bsdf_pre->GetRadiance(hit_pre.texcoord);
    return weight_direct * (radiance * rec.attenuation / pdf_direct);
}

QUALIFIER_D_H SamplingRecord Integrator::EvaluateRay(const Vec3 &wi,
                                                     const Vec3 &wo,
                                                     const Hit &hit,
                                                     Bsdf *bsdf) const
{
    SamplingRecord rec;
    rec.wi = wi;
    rec.wo = wo;
    rec.texcoord = hit.texcoord;
    rec.position = hit.position;
    if (bsdf)
    {
        rec.inside = hit.inside;
        rec.normal = hit.normal;
        rec.tangent = hit.tangent;
        rec.bitangent = hit.bitangent;
        if (Dot(-wi, hit.normal) < 0.0f)
        {
            rec.inside = !rec.inside;
            rec.normal = -rec.normal;
        }
        bsdf->Evaluate(&rec);
    }
    else
    {
        rec.pdf = 1;
        rec.attenuation = Vec3(1);
        rec.valid = true;
    }
    return rec;
}

QUALIFIER_D_H SamplingRecord Integrator::SampleRay(const Vec3 &wo,
                                                   const Hit &hit, Bsdf *bsdf,
                                                   const Vec3 &xi) const
{
    SamplingRecord rec;
    rec.wo = wo;
    rec.texcoord = hit.texcoord;
    rec.position = hit.position;
    if (bsdf != nullptr)
    {
        rec.inside = hit.inside;
        rec.normal = hit.normal;
        rec.tangent = hit.tangent;
        rec.bitangent = hit.bitangent;
        if (Dot(wo, hit.normal) < 0.0f)
        {
            rec.inside = !rec.inside;
            rec.normal = -rec.normal;
        }
        bsdf->Sample(xi, &rec);
    }
    else
    {
        rec.wi = wo;
        rec.pdf = 1.0f;
        rec.attenuation = Vec3(1.0f);
        rec.valid = true;
    }
    return rec;
}

} // namespace rt