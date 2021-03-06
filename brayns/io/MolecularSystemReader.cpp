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

#include "MolecularSystemReader.h"

#include <brayns/common/log.h>
#include <brayns/common/scene/Scene.h>
#include <brayns/io/ProteinLoader.h>
#include <brayns/io/MeshLoader.h>
#include <fstream>

namespace brayns
{

MolecularSystemReader::MolecularSystemReader(
    const GeometryParameters& geometryParameters )
    : _geometryParameters( geometryParameters )
{
}

bool MolecularSystemReader::import( Scene& scene )
{
    _nbProteins = 0;
    if( !_loadConfiguration( ))
        return false;
    if( !_loadProteins( ))
        return false;
    if( !_loadPositions( ))
        return false;

    BRAYNS_INFO << "Total number of different proteins: " << _proteins.size() << std::endl;
    BRAYNS_INFO << "Total number of proteins          : " << _nbProteins << std::endl;

    return _createScene( scene );
}

bool MolecularSystemReader::_createScene( Scene& scene )
{
    MeshQuality quality;
    switch( _geometryParameters.getGeometryQuality( ))
    {
    case GeometryQuality::medium:
        quality = MQ_QUALITY;
        break;
    case GeometryQuality::high:
        quality = MQ_MAX_QUALITY;
        break;
    default:
        quality = MQ_FAST ;
        break;
    }

    MeshLoader meshLoader;
    uint64_t proteinCount = 0;
    for( const auto& proteinPosition: _proteinPositions )
    {
        BRAYNS_PROGRESS( proteinCount, _nbProteins );

        const auto& protein = _proteins.find( proteinPosition.first );
        if( !_proteinFolder.empty( ))
            // Load PDB files
            for( const auto& position: proteinPosition.second )
            {
                const auto pdbFilename = _proteinFolder + '/' + protein->second + ".pdb";
                ProteinLoader loader( _geometryParameters );
                loader.importPDBFile( pdbFilename, position, proteinCount, scene );
                ++proteinCount;
            }

        if( !_meshFolder.empty( ))
            // Load meshes
            for( const auto& position: proteinPosition.second )
            {
                const auto objFilename = _meshFolder + '/' + protein->second + ".obj";
                MeshContainer MeshContainer =
                {
                    scene.getTriangleMeshes(),
                    scene.getMaterials(),
                    scene.getWorldBounds()
                };

                const size_t material =
                    _geometryParameters.getColorScheme() == ColorScheme::protein_by_id ?
                    proteinCount % (NB_MAX_MATERIALS - NB_SYSTEM_MATERIALS) :
                    NO_MATERIAL;

                // Scale mesh to match PDB units. PDB are in angstrom, and positions are
                // in micrometers
                const float scale = 0.0001f;
                meshLoader.importMeshFromFile(
                    objFilename, MeshContainer, quality,
                    position, Vector3f( scale, scale, scale ), material );

                if( _proteinFolder.empty( ))
                    ++proteinCount;
            }
    }

    // Update materials
    if( _geometryParameters.getColorScheme() != ColorScheme::protein_by_id )
    {
        size_t index = 0;
        for( const auto& material: scene.getMaterials( ))
        {
            ProteinLoader loader( _geometryParameters );
            material->setColor( loader.getMaterialKd( index ));
            ++index;
        }
    }
    return true;
}

bool MolecularSystemReader::_loadConfiguration()
{
    // Load molecular system configuration
    const auto& configuration = _geometryParameters.getMolecularSystemConfig();
    std::ifstream configurationFile( configuration, std::ios::in );
    if( !configurationFile.good( ))
    {
        BRAYNS_ERROR << "Could not open file " << configuration << std::endl;
        return false;
    }

    std::map< std::string, std::string > parameters;
    std::string line;
    while( std::getline( configurationFile, line ))
    {
        std::stringstream lineStream( line );
        std::string key, value;
        lineStream >> key >> value;
        parameters[ key ] = value;
    }
    configurationFile.close();

    _proteinFolder = parameters["ProteinFolder"];
    _meshFolder = parameters["MeshFolder"];
    _descriptorFilename = parameters["SystemDescriptor"];
    _positionsFilename = parameters["ProteinPositions"];

    BRAYNS_INFO << "Loading biological assembly" << std::endl;
    BRAYNS_INFO << "Protein folder    : " << _proteinFolder << std::endl;
    BRAYNS_INFO << "Mesh folder       : " << _meshFolder << std::endl;
    BRAYNS_INFO << "System descriptor : " << _descriptorFilename << std::endl;
    BRAYNS_INFO << "Protein positions : " << _positionsFilename << std::endl;
    return true;
}

bool MolecularSystemReader::_loadProteins( )
{
    std::ifstream descriptorFile( _descriptorFilename, std::ios::in );
    if( !descriptorFile.good( ))
    {
        BRAYNS_ERROR << "Could not open file " << _descriptorFilename << std::endl;
        return false;
    }

    // Load list of proteins
    std::string line;
    while( std::getline( descriptorFile, line ))
    {
        std::stringstream lineStream( line );
        std::string protein;
        size_t id;
        size_t instances;
        lineStream >> protein >> id >> instances;
        if( protein.empty( ))
            continue;

        _proteins[id] = protein;

        if( _proteinFolder.empty( ))
            continue;

        const auto pdbFilename( _proteinFolder + '/' + protein + ".pdb" );
        std::ifstream pdbFile( pdbFilename, std::ios::in );
        if( pdbFile.good( ))
            pdbFile.close();
        else
        {
            // PDB file not present in folder, download it from RCSB.org
            std::string command;
            command = "wget http://www.rcsb.org/pdb/files/";
            command += protein;
            command += ".pdb";
            command += " -P ";
            command += _proteinFolder;
            int status = system( command.c_str( ));
            BRAYNS_INFO << command << ": " << status << std::endl;
        }
    }
    descriptorFile.close();
    return true;
}

bool MolecularSystemReader::_loadPositions()
{
    // Load proteins according to specified positions
    std::ifstream filePositions( _positionsFilename, std::ios::in );
    if( !filePositions.good( ))
    {
        BRAYNS_ERROR << "Could not open file " << _positionsFilename << std::endl;
        return false;
    }

    // Load positions
    _nbProteins = 0;
    std::string line;
    while( std::getline( filePositions, line ))
    {
        std::stringstream lineStream( line );
        size_t id;
        Vector3f position;
        lineStream >> id >> position.x() >> position.y() >> position.z();

        if( _proteins.find(id) != _proteins.end( ))
        {
            auto& proteinPosition = _proteinPositions[ id ];
            proteinPosition.push_back( position );
            ++_nbProteins;
        }
    }
    filePositions.close();
    return true;
}

}
