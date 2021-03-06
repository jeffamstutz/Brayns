/* Copyright (c) 2015-2016, EPFL/Blue Brain Project
 * All rights reserved. Do not distribute without permission.
 * Responsible Author: Cyrille Favreau <cyrille.favreau@epfl.ch>
 *
 * This file is part of Brayns <https://github.com/BlueBrain/Brayns>
 *
 * This library is free software; you can redistribute it and/or modify it under
 * the terms of the GNU Lesser General Public License version 3.0 as published
 * by the Free Software Foundation.
 *
 * This library is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
 * FOR A PARTICULAR PURPOSE.  See the GNU Lesser General Public License for more
 * details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with this library; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */

#ifndef OPTIXSCENE_H
#define OPTIXSCENE_H

#include <brayns/common/types.h>
#include <brayns/common/scene/Scene.h>

#include <optixu/optixpp_namespace.h>

namespace brayns
{

struct BasicLight
{
  optix::float3 pos;
  optix::float3 color;
  int casts_shadow;
  int padding;      // make this structure 32 bytes -- powers of two are your friend!
};

/**

   OptiX specific scene

   This object is the OptiX specific implementation of a scene

*/
class OptiXScene : public brayns::Scene
{

public:

    OptiXScene(
        Renderers renderer,
        ParametersManager& parametersManager,
        optix::Context& context );

    ~OptiXScene();

    /** @copydoc Scene::commit */
    void commit() final;

    /** @copydoc Scene::buildGeometry */
    void buildGeometry() final;

    /** @copydoc Scene::commitLights */
    void commitLights() final;

    /** @copydoc Scene::commitMaterials */
    void commitMaterials( const bool updateOnly = false ) final;

    /** @copydoc Scene::commitSimulationData */
    void commitSimulationData() final;

    /** @copydoc Scene::commitVolumeData */
    void commitVolumeData() final;

    /** @copydoc Scene::commitTransferFunctionData */
    void commitTransferFunctionData() final;

    /** @copydoc Scene::reset */
    void reset() final;

    /** @copydoc Scene::saveSceneToCacheFile */
    void saveSceneToCacheFile() final;

private:

    void _processVolumeAABBGeometry();
    bool _createTexture2D( const std::string& textureName );

    uint64_t _getBvhSize( const uint64_t nbElements ) const;

    uint64_t _processParametricGeometries();
    uint64_t _processMeshes();

    optix::Context& _context;
    optix::GeometryGroup _geometryGroup;
    std::vector< optix::GeometryInstance > _geometryInstances;
    std::vector< optix::Material > _optixMaterials;
    optix::Buffer _lightBuffer;
    std::vector< BasicLight > _optixLights;
    std::string _accelerationStructure;
    optix::Buffer _colorMapBuffer;

    // Spheres
    std::map< size_t, floats > _serializedSpheresData;
    std::map< size_t, size_t > _serializedSpheresDataSize;
    std::map< size_t, size_t > _timestampSpheresIndices;
    std::map< size_t, optix::Buffer > _spheresBuffers;
    std::map< size_t, optix::Geometry > _optixSpheres;

    // Cylinders
    std::map< size_t, floats > _serializedCylindersData;
    std::map< size_t, size_t > _serializedCylindersDataSize;
    std::map< size_t, size_t > _timestampCylindersIndices;
    std::map< size_t, optix::Buffer > _cylindersBuffers;
    std::map< size_t, optix::Geometry > _optixCylinders;

    // Cones
    std::map< size_t, floats > _serializedConesData;
    std::map< size_t, size_t > _serializedConesDataSize;
    std::map< size_t, size_t > _timestampConesIndices;
    std::map< size_t, optix::Buffer > _conesBuffers;
    std::map< size_t, optix::Geometry > _optixCones;

    // Triangle meshes
    optix::Geometry _mesh;
    optix::Buffer _verticesBuffer;
    optix::Buffer _indicesBuffer;
    optix::Buffer _normalsBuffer;
    optix::Buffer _textureCoordsBuffer;
    optix::Buffer _materialsBuffer;

    // Volume
    optix::Buffer _volumeBuffer;

    // Textures
    std::map<std::string, optix::Buffer> _optixTextures;
    std::map<std::string, optix::TextureSampler> _optixTextureSamplers;

};

}
#endif // OPTIXSCENE_H
