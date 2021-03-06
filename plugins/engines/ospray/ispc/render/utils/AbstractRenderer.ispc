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

#include <plugins/engines/ospray/ispc/render/utils/AbstractRenderer.ih>

inline bool launchRandomRay(
    const uniform AbstractRenderer* uniform self,
    const varying Ray& ray,
    varying ScreenSample& sample,
    const varying vec3f& intersection,
    const varying vec3f& normal,
    DifferentialGeometry& geometry,
    varying vec3f& backgroundColor,
    varying float& distanceToIntersection,
    varying vec3f& randomDirection )
{
    randomDirection = getRandomVector( sample, normal, self->randomNumber );
    backgroundColor = self->bgColor;

    if( dot( randomDirection, normal ) < 0.01f )
        // Invert direction of random ray direction is opposite to surface
        // normal
        randomDirection = randomDirection * -1.f;

    varying Ray randomRay;
    setRay( randomRay, intersection, randomDirection );
    randomRay.t0 = self->super.epsilon;
    randomRay.time = ray.time;
    randomRay.t = ray.t;
    randomRay.primID = -1;
    randomRay.geomID = -1;
    randomRay.instID = -1;

    traceRay( self->super.model, randomRay );
    if( randomRay.geomID < 0 )
    {
        // No intersection
        backgroundColor =
            make_vec3f(
                skyboxMapping( (Renderer *)self, randomRay, self->numMaterials, self->materials ));
        return false;
    }

    // Random ray hits a primitive
    distanceToIntersection = self->super.epsilon; //randomRay.t * randomRay.t;
    postIntersect(
        self->super.model, geometry,
        randomRay,
        DG_NG|DG_NS|DG_NORMALIZE|DG_MATERIALID|
        DG_COLOR|DG_TEXCOORD);
    return true;
}

inline vec3f refractedVector(
    const varying vec3f& direction,
    const varying vec3f& normal,
    const varying float n1,
    const varying float n2)
{
    if( n2 == 0.f )
        return direction;
    const varying float eta = n1 / n2;
    const varying float cos1 = -dot( direction, normal );
    const varying float cos2 = 1.f - eta * eta * ( 1.f - cos1 * cos1 );
    if( cos2 > 0.f )
        return eta * direction + ( eta * cos1 - sqrt( cos2 )) * normal;
    return direction;
}

inline bool indirectShading(
    const uniform AbstractRenderer* uniform self,
    Ray& ray,
    varying ScreenSample& sample,
    const varying vec3f& intersection,
    const varying vec3f& normal,
    DifferentialGeometry& geometry,
    varying vec3f& indirectShadingColor,
    varying float& indirectShadingPower )
{
    varying vec3f backgroundColor;
    varying float distanceToIntersection = infinity;
    indirectShadingPower = 0.f;

    // Launch a random ray
    varying vec3f randomDirection;
    if( launchRandomRay(
        (AbstractRenderer*)self, ray, sample, intersection, normal,
        geometry, backgroundColor, distanceToIntersection, randomDirection ))
    {
        // Determine material of intersected geometry
        const uniform Material* material = geometry.material;
        const uniform ExtendedOBJMaterial* objMaterial =
            ( const uniform ExtendedOBJMaterial* )material;
        if( objMaterial )
            foreach_unique( mat in objMaterial )
            {
                // Check if material emits light and is random rays hits the
                // surface in the opposite direction from the surface normal
                if( mat->a != 0.f && dot( geometry.Ns, randomDirection ) < 0.f )
                {
                    // Light emitting material
                    vec3f localDiffuseColor = mat->Kd;
                    if( mat->map_Kd )
                        localDiffuseColor = get3f( mat->map_Kd, geometry.st );
                    vec3f Ka = make_vec3f( mat->a );
                    indirectShadingPower = mat->a;

                    if( mat->map_a )
                    {
                        Ka = get3f( mat->map_a, geometry.st );
                        indirectShadingPower = reduce_max( Ka );
                    }
                    // Returned color is the one of the emitting material
                    // with weigth according to distance to intersection
                    indirectShadingColor = localDiffuseColor * Ka;
                    indirectShadingPower /= distanceToIntersection;
                }
                else
                {
                    // Material does not emit light, occlusion is considered
                    // according to surface localOpacity and distance to intersection
                    indirectShadingColor = make_vec3f( mat->d );
                    indirectShadingPower = -mat->d / distanceToIntersection;
                }
            }
        else
        {
            // Material does not emit light, occlusion is considered
            // according to surface localOpacity and distance to intersection
            indirectShadingColor = make_vec3f( geometry.color.w );
            indirectShadingPower = -geometry.color.w / distanceToIntersection;
        }
        return true;
    }
    else
    {
        indirectShadingPower = DEFAULT_SKYBOX_INTENSITY;
        indirectShadingColor = backgroundColor;
    }
    return false;
}

inline float shadedLightIntensity(
    const uniform AbstractRenderer* uniform self,
    Ray& ray,
    varying ScreenSample& sample,
    const varying vec3f& intersection,
    const varying vec3f& normal,
    varying vec3f& lightDirection )
{
    if( self->softShadowsEnabled )
    {
        // Slightly alter light direction for Soft shadows
        const varying vec3f ss =
            getRandomVector( sample, normal, self->randomNumber );
        lightDirection = lightDirection + ss * 0.1f;
    }

    Ray shadowRay;
    setRay( shadowRay, intersection, lightDirection );
    shadowRay.t0 = self->super.epsilon;
    shadowRay.time = sample.ray.time;
    shadowRay.t = ray.t;

    varying float intensity = 1.f;
    const varying float maxt = ray.t;
    varying int depth = 0;
    varying bool moreRebounds = true;

    // Light attenuation altered by volume
    if( self->volumeData )
    {
        const vec4f volumetricValue = getVolumeContribution( self, shadowRay );
        intensity -= volumetricValue.w;
        moreRebounds = ( volumetricValue.w < 1.f );
    }

    while( moreRebounds && depth < NB_MAX_REBOUNDS )
    {
        traceRay( self->super.model, shadowRay );

        if( shadowRay.geomID >= 0 )
        {
            DifferentialGeometry dg;
            postIntersect( self->super.model, dg, shadowRay, DG_MATERIALID|DG_TEXCOORD|DG_COLOR );

            uniform ExtendedOBJMaterial* objMaterial = ( uniform ExtendedOBJMaterial* )dg.material;

            varying float materialOpacity = 1.f;

            if( objMaterial == 0 )
                materialOpacity = 1.f - dg.color.w;
            else
            {
                foreach_unique( mat in objMaterial )
                {
                    materialOpacity = mat->d;
                    if( mat->map_d != 0 )
                    {
                        vec4f d_map = mat->map_d->get( mat->map_d, dg.st );
                        materialOpacity = d_map.x;
                    }
                    else if( mat->map_Kd != 0 )
                    {
                        vec4f localDiffuseColor_map = mat->map_Kd->get( mat->map_Kd, dg.st );
                        materialOpacity = 1.f - localDiffuseColor_map.w;
                    }
                }
            }

            intensity -= materialOpacity;

            if( intensity < ALPHA_THRESHOLD )
                moreRebounds = false;

            shadowRay.t0 = shadowRay.t + self->super.epsilon;
            shadowRay.t = maxt;
            shadowRay.primID = -1;
            shadowRay.geomID = -1;
            shadowRay.instID = -1;
            ++depth;
       }
       else
            moreRebounds = false;
    }
    return intensity;
}

// AABB-Ray intersection ( http://prideout.net/blog/?p=64 ).
inline bool intersectBox(
    const uniform AbstractRenderer* uniform self,
    Ray& ray,
    float &tnear, float& tfar )
{
    //We need to avoid division by zero in "vec3 invR = 1.0 / ray.dir;"
    if( ray.dir.x == 0.f )
        ray.dir.x = self->super.epsilon;

    if( ray.dir.y == 0.f )
        ray.dir.y = self->super.epsilon;

    if( ray.dir.z == 0.f )
        ray.dir.z = self->super.epsilon;

    const vec3f aabbMin = self->volumeOffset;
    const vec3f aabbMax =
        self->volumeOffset + self->volumeDimensions.x * self->volumeElementSpacing;

    const vec3f invR = 1.f / ray.dir;
    const vec3f tbot = invR * ( aabbMin - ray.org );
    const vec3f ttop = invR * ( aabbMax - ray.org );
    const vec3f tmin = min( ttop, tbot );
    const vec3f tmax = max( ttop, tbot );

    vec2f t = max( make_vec2f( tmin.x, tmin.x), make_vec2f( tmin.y, tmin.z) );
    tnear = max( t.x, t.y );

    t = min( make_vec2f( tmax.x, tmax.x ), make_vec2f( tmax.y, tmax.z) );
    tfar = min( t.x, t.y );

    return tnear <= tfar;
}

inline varying vec4f getVolumeContribution(
    const uniform AbstractRenderer* uniform self,
    Ray& ray )
{
    vec3f pathColor = make_vec3f( 0.f );
    const vec3i dimensions = self->volumeDimensions;

    float t = ray.t0 + self->volumeEpsilon;
    const float tMax = ray.t - self->volumeEpsilon;

    float pathAlpha = 0.f;
    while( t < tMax && pathAlpha < 1.f )
    {
        vec3f point = (( ray.org + ray.dir * t ) - self->volumeOffset) / self->volumeElementSpacing;

        if( point.x > 0.f && point.x < dimensions.x &&
            point.y > 0.f && point.y < dimensions.y &&
            point.z > 0.f && point.z < dimensions.z )
        {
            uint64 index = (uint64)(
                (uint64)floor( point.x ) +
                (uint64)floor( point.y ) * dimensions.x +
                (uint64)floor( point.z ) * dimensions.x * dimensions.y );

            const uint8 voxelValue = self->volumeData[ index ];
            if( self->colorMap )
            {
                const float normalizedValue =
                    self->colorMapSize * ( voxelValue - self->colorMapMinValue ) /
                    self->colorMapRange;
                const vec4f colorMapColor = self->colorMap[ normalizedValue ];
                const vec3f voxelColor =
                    make_vec3f( colorMapColor.x, colorMapColor.y, colorMapColor.z );
                const float alphaMagic = self->volumeEpsilon / self->volumeDiag;
                const float alpha = 1.f - pow(
                    1.f - min( colorMapColor.w, 1.f - 1.f / (float)self->colorMapSize), alphaMagic );
                pathColor = pathColor + ( voxelColor * alpha * ( 1.f - pathAlpha ));
                pathAlpha = pathAlpha + ( alpha * ( 1.f - pathAlpha ));
            }
            else
            {
                float alpha = 1.f;
                vec3f voxelColor = { point.x, point.y, point.z };

                pathColor = pathColor + ( voxelColor * alpha * ( 1.f - pathAlpha ));
                pathAlpha = pathAlpha + ( alpha * ( 1.f - pathAlpha ));
            }
        }
        t += self->volumeEpsilon;
    }
    return make_vec4f( pathColor.x, pathColor.y, pathColor.z, pathAlpha );
}
