#include <cmath>
#include <pcl/pcl_config.h>
// PCL 1.8 supplies a log2f fallback that conflicts with the modern MSVC CRT.
#if defined(_MSC_VER) && PCL_MAJOR_VERSION == 1 && PCL_MINOR_VERSION < 9
#define log2f pcl_legacy_log2f
#include <pcl/pcl_macros.h>
#undef log2f
#endif
#include <pcl/features/normal_3d.h>
#include <pcl/kdtree/kdtree_flann.h>
#include <algorithm>
#include <cstdio>
#include <exception>
#include <limits>
#include <stdexcept>
#include <vector>

#ifdef _WIN32
#define NORMALS_API extern "C" __declspec(dllexport)
#else
#define NORMALS_API extern "C" __attribute__((visibility("default")))
#endif

NORMALS_API const char *pcl_normals_version()
{
  return PCL_BENCH_VERSION;
}

// Explicitly exclude the query index: default PCL k-search otherwise includes it.
NORMALS_API int pcl_normals_compute(const float *xyz, int count, int k, int threads,
                                   const float *centroid, float *normals, int *neighbors,
                                   char *error, int error_size)
{
  try
  {
    if (!xyz || !centroid || !normals || count < 0 || k < 3 || threads < 1)
      throw std::invalid_argument("Invalid normal estimation input");
    if (count < 4)
    {
      std::fill(normals, normals + count * 3, 0.0f);
      return 0;
    }
    k = std::min(k, count - 1);
    using Point = pcl::PointXYZ;
    pcl::PointCloud<Point>::Ptr cloud(new pcl::PointCloud<Point>);
    cloud->resize(count);
    cloud->is_dense = true;
    for (int i = 0; i < count; ++i)
    {
      for (int axis = 0; axis < 3; ++axis)
      {
        if (!std::isfinite(xyz[i * 3 + axis]))
          throw std::invalid_argument("Point coordinates must be finite");
        cloud->points[i].data[axis] = xyz[i * 3 + axis];
      }
    }
    pcl::KdTreeFLANN<Point> tree;
    tree.setInputCloud(cloud);
    tree.setEpsilon(0.0f);
    int invalid = 0;
#pragma omp parallel for num_threads(threads) schedule(static) reduction(+:invalid)
    for (int i = 0; i < count; ++i)
    {
      std::vector<int> candidates(k + 1), indices;
      std::vector<float> distances(k + 1);
      const int found = tree.nearestKSearch(cloud->points[i], k + 1, candidates, distances);
      indices.reserve(k);
      for (int j = 0; j < found && static_cast<int>(indices.size()) < k; ++j)
        if (candidates[j] != i) indices.push_back(candidates[j]);
      Eigen::Vector4f plane;
      float curvature;
      const bool ok = static_cast<int>(indices.size()) == k &&
                      pcl::computePointNormal(*cloud, indices, plane, curvature);
      if (neighbors)
        for (int j = 0; j < k; ++j)
          neighbors[i * k + j] = j < static_cast<int>(indices.size()) ? indices[j] : -1;
      if (!ok || !plane.head<3>().allFinite() || plane.head<3>().norm() < 1e-8f)
      {
        ++invalid;
        for (int axis = 0; axis < 3; ++axis)
          normals[i * 3 + axis] = std::numeric_limits<float>::quiet_NaN();
        continue;
      }
      plane.head<3>().normalize();
      float dot = 0.0f;
      for (int axis = 0; axis < 3; ++axis)
        dot += plane[axis] * (xyz[i * 3 + axis] - centroid[axis]);
      for (int axis = 0; axis < 3; ++axis)
        normals[i * 3 + axis] = dot < 0.0f ? -plane[axis] : plane[axis];
    }
    return invalid;
  }
  catch (const std::exception &e)
  {
    if (error && error_size > 0) std::snprintf(error, error_size, "%s", e.what());
    return -1;
  }
}
