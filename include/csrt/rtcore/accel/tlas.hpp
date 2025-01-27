#ifndef CSRT__RTCORE__ACCEL_TLAS_HPP
#define CSRT__RTCORE__ACCEL_TLAS_HPP

#include "../hit.hpp"
#include "../instance.hpp"
#include "../ray.hpp"
#include "bvh_builder.hpp"

namespace csrt
{

class TLAS
{
public:
    QUALIFIER_D_H TLAS();
    QUALIFIER_D_H TLAS(const Instance *instances, const BvhNode *node_buffer);

    QUALIFIER_D_H Hit Intersect(Bsdf *bsdf_buffer, uint32_t *map_instance_bsdf,
                                uint32_t *seed, Ray *ray) const;

    QUALIFIER_D_H bool IntersectAny(Bsdf *bsdf_buffer,
                                    uint32_t *map_instance_bsdf, uint32_t *seed,
                                    Ray *ray) const;

private:
    const BvhNode *nodes_;
    const Instance *instances_;
};

} // namespace csrt

#endif