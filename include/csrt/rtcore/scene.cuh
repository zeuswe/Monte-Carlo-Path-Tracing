#pragma once

#include <vector>

#include "../utils.cuh"
#include "accel/tlas.cuh"
#include "accel/bvh_builder.cuh"
#include "instance.cuh"
#include "primitives/primitive.cuh"

namespace csrt
{

class Scene
{
public:
    Scene(const BackendType backend_type);
    ~Scene();

    void AddInstance(const Instance::Info &info);
    void Commit();

    TLAS *GetTlas() { return tlas_; };
    Instance *GetInstances() { return instances_; }
    float *GetPdfAreaList() { return list_pdf_area_; }

private:
    void CommitPrimitives();
    void CommitInstances();

    void CommitCube(const uint32_t id);
    void CommitSphere(const uint32_t id);
    void CommitRectangle(const uint32_t id);
    void CommitMeshes(const uint32_t id);

    void SetupMeshes(Instance::Info::Meshes info_meshes,
                     std::vector<Primitive::Data> *list_data_primitve,
                     std::vector<float> *areas);

    BackendType backend_type_;
    TLAS *tlas_;
    Instance *instances_;
    BLAS *list_blas_;
    float *list_pdf_area_;
    Primitive *primitives_;
    BvhNode *nodes_;
    uint64_t num_primitive_;
    uint64_t num_node_;
    std::vector<Instance::Info> list_info_instance_;
    std::vector<uint64_t> list_offset_primitive_;
    std::vector<uint64_t> list_offset_node_;
};

} // namespace csrt