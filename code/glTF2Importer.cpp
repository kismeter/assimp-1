/*
Open Asset Import Library (assimp)
----------------------------------------------------------------------

Copyright (c) 2006-2018, assimp team


All rights reserved.

Redistribution and use of this software in source and binary forms,
with or without modification, are permitted provided that the
following conditions are met:

* Redistributions of source code must retain the above
copyright notice, this list of conditions and the
following disclaimer.

* Redistributions in binary form must reproduce the above
copyright notice, this list of conditions and the
following disclaimer in the documentation and/or other
materials provided with the distribution.

* Neither the name of the assimp team, nor the names of its
contributors may be used to endorse or promote products
derived from this software without specific prior
written permission of the assimp team.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
"AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
(INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

----------------------------------------------------------------------
*/

#ifndef ASSIMP_BUILD_NO_GLTF_IMPORTER

#include "glTF2Importer.h"
#include <assimp/StringComparison.h>
#include <assimp/StringUtils.h>

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/ai_assert.h>
#include <assimp/DefaultLogger.hpp>
#include <assimp/importerdesc.h>
#include <assimp/CreateAnimMesh.h>

#include <memory>
#include <unordered_map>

#include "MakeVerboseFormat.h"

#include "glTF2Asset.h"
// This is included here so WriteLazyDict<T>'s definition is found.
#include "glTF2AssetWriter.h"
#include <rapidjson/document.h>
#include <rapidjson/rapidjson.h>

using namespace Assimp;
using namespace glTF2;

namespace {
    // generate bitangents from normals and tangents according to spec
    struct Tangent {
        aiVector3D xyz;
        ai_real w;
    };


    //todo: handle other permissible types
    struct Joint {
       unsigned short jointinfo[4];
    };

    //todo: handle other permissible typess
    struct Weight {
        float weightinfo[4];
    };

    //custom comparator
    struct mycomp{
        bool operator()(  const aiString &a, const  aiString &b) const{
            return a.length > b.length;
        }
    };


} // namespace


//
// glTF2Importer
//

static const aiImporterDesc desc = {
    "glTF2 Importer",
    "",
    "",
    "",
    aiImporterFlags_SupportTextFlavour | aiImporterFlags_SupportBinaryFlavour | aiImporterFlags_LimitedSupport | aiImporterFlags_Experimental,
    0,
    0,
    0,
    0,
    "gltf glb"
};

glTF2Importer::glTF2Importer()
: BaseImporter()
, meshOffsets()
, embeddedTexIdxs()
, mScene( NULL ) {
    // empty
}

glTF2Importer::~glTF2Importer() {
    // empty
}

const aiImporterDesc* glTF2Importer::GetInfo() const
{
    return &desc;
}

bool glTF2Importer::CanRead(const std::string& pFile, IOSystem* pIOHandler, bool /* checkSig */) const
{
    const std::string &extension = GetExtension(pFile);

    if (extension != "gltf" && extension != "glb")
        return false;

    if (pIOHandler) {
        glTF2::Asset asset(pIOHandler);
        asset.Load(pFile, extension == "glb");
        std::string version = asset.asset.version;
        return !version.empty() && version[0] == '2';
    }

    return false;
}

static aiTextureMapMode ConvertWrappingMode(SamplerWrap gltfWrapMode)
{
    switch (gltfWrapMode) {
        case SamplerWrap::Mirrored_Repeat:
            return aiTextureMapMode_Mirror;

        case SamplerWrap::Clamp_To_Edge:
            return aiTextureMapMode_Clamp;

        case SamplerWrap::UNSET:
        case SamplerWrap::Repeat:
        default:
            return aiTextureMapMode_Wrap;
    }
}

//static void CopyValue(const glTF2::vec3& v, aiColor3D& out)
//{
//    out.r = v[0]; out.g = v[1]; out.b = v[2];
//}

static void CopyValue(const glTF2::vec4& v, aiColor4D& out)
{
    out.r = v[0]; out.g = v[1]; out.b = v[2]; out.a = v[3];
}

/*static void CopyValue(const glTF2::vec4& v, aiColor3D& out)
{
    out.r = v[0]; out.g = v[1]; out.b = v[2];
}*/

static void CopyValue(const glTF2::vec3& v, aiColor4D& out)
{
    out.r = v[0]; out.g = v[1]; out.b = v[2]; out.a = 1.0;
}

static void CopyValue(const glTF2::vec3& v, aiVector3D& out)
{
    out.x = v[0]; out.y = v[1]; out.z = v[2];
}

static void CopyValue(const glTF2::vec4& v, aiQuaternion& out)
{
    out.x = v[0]; out.y = v[1]; out.z = v[2]; out.w = v[3];
}

static void CopyValue(const glTF2::mat4& v, aiMatrix4x4& o)
{
    o.a1 = v[ 0]; o.b1 = v[ 1]; o.c1 = v[ 2]; o.d1 = v[ 3];
    o.a2 = v[ 4]; o.b2 = v[ 5]; o.c2 = v[ 6]; o.d2 = v[ 7];
    o.a3 = v[ 8]; o.b3 = v[ 9]; o.c3 = v[10]; o.d3 = v[11];
    o.a4 = v[12]; o.b4 = v[13]; o.c4 = v[14]; o.d4 = v[15];
}

inline void SetMaterialColorProperty(Asset& /*r*/, vec4& prop, aiMaterial* mat, const char* pKey, unsigned int type, unsigned int idx)
{
    aiColor4D col;
    CopyValue(prop, col);
    mat->AddProperty(&col, 1, pKey, type, idx);
}

inline void SetMaterialColorProperty(Asset& /*r*/, vec3& prop, aiMaterial* mat, const char* pKey, unsigned int type, unsigned int idx)
{
    aiColor4D col;
    CopyValue(prop, col);
    mat->AddProperty(&col, 1, pKey, type, idx);
}

inline void SetMaterialTextureProperty(std::vector<int>& embeddedTexIdxs, Asset& /*r*/, glTF2::TextureInfo prop, aiMaterial* mat, aiTextureType texType, unsigned int texSlot = 0)
{
    if (prop.texture && prop.texture->source) {
        aiString uri(prop.texture->source->uri);

        int texIdx = embeddedTexIdxs[prop.texture->source.GetIndex()];
        if (texIdx != -1) { // embedded
            // setup texture reference string (copied from ColladaLoader::FindFilenameForEffectTexture)
            uri.data[0] = '*';
            uri.length = 1 + ASSIMP_itoa10(uri.data + 1, MAXLEN - 1, texIdx);
        }

        mat->AddProperty(&uri, AI_MATKEY_TEXTURE(texType, texSlot));
        mat->AddProperty(&prop.texCoord, 1, _AI_MATKEY_GLTF_TEXTURE_TEXCOORD_BASE, texType, texSlot);

        if (prop.texture->sampler) {
            Ref<Sampler> sampler = prop.texture->sampler;

            aiString name(sampler->name);
            aiString id(sampler->id);

            mat->AddProperty(&name, AI_MATKEY_GLTF_MAPPINGNAME(texType, texSlot));
            mat->AddProperty(&id, AI_MATKEY_GLTF_MAPPINGID(texType, texSlot));

            aiTextureMapMode wrapS = ConvertWrappingMode(sampler->wrapS);
            aiTextureMapMode wrapT = ConvertWrappingMode(sampler->wrapT);
            mat->AddProperty(&wrapS, 1, AI_MATKEY_MAPPINGMODE_U(texType, texSlot));
            mat->AddProperty(&wrapT, 1, AI_MATKEY_MAPPINGMODE_V(texType, texSlot));

            if (sampler->magFilter != SamplerMagFilter::UNSET) {
                mat->AddProperty(&sampler->magFilter, 1, AI_MATKEY_GLTF_MAPPINGFILTER_MAG(texType, texSlot));
            }

            if (sampler->minFilter != SamplerMinFilter::UNSET) {
                mat->AddProperty(&sampler->minFilter, 1, AI_MATKEY_GLTF_MAPPINGFILTER_MIN(texType, texSlot));
            }
        }
    }
}

inline void SetMaterialTextureProperty(std::vector<int>& embeddedTexIdxs, Asset& r, glTF2::NormalTextureInfo& prop, aiMaterial* mat, aiTextureType texType, unsigned int texSlot = 0)
{
    SetMaterialTextureProperty( embeddedTexIdxs, r, (glTF2::TextureInfo) prop, mat, texType, texSlot );

    if (prop.texture && prop.texture->source) {
         mat->AddProperty(&prop.scale, 1, AI_MATKEY_GLTF_TEXTURE_SCALE(texType, texSlot));
    }
}

inline void SetMaterialTextureProperty(std::vector<int>& embeddedTexIdxs, Asset& r, glTF2::OcclusionTextureInfo& prop, aiMaterial* mat, aiTextureType texType, unsigned int texSlot = 0)
{
    SetMaterialTextureProperty( embeddedTexIdxs, r, (glTF2::TextureInfo) prop, mat, texType, texSlot );

    if (prop.texture && prop.texture->source) {
        mat->AddProperty(&prop.strength, 1, AI_MATKEY_GLTF_TEXTURE_STRENGTH(texType, texSlot));
    }
}

static aiMaterial* ImportMaterial(std::vector<int>& embeddedTexIdxs, Asset& r, Material& mat)
{
    aiMaterial* aimat = new aiMaterial();

   if (!mat.name.empty()) {
        aiString str(mat.name);

        aimat->AddProperty(&str, AI_MATKEY_NAME);
    }

    SetMaterialColorProperty(r, mat.pbrMetallicRoughness.baseColorFactor, aimat, AI_MATKEY_COLOR_DIFFUSE);
    SetMaterialColorProperty(r, mat.pbrMetallicRoughness.baseColorFactor, aimat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_FACTOR);

    SetMaterialTextureProperty(embeddedTexIdxs, r, mat.pbrMetallicRoughness.baseColorTexture, aimat, aiTextureType_DIFFUSE);
    SetMaterialTextureProperty(embeddedTexIdxs, r, mat.pbrMetallicRoughness.baseColorTexture, aimat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_BASE_COLOR_TEXTURE);

    SetMaterialTextureProperty(embeddedTexIdxs, r, mat.pbrMetallicRoughness.metallicRoughnessTexture, aimat, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLICROUGHNESS_TEXTURE);

    aimat->AddProperty(&mat.pbrMetallicRoughness.metallicFactor, 1, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_METALLIC_FACTOR);
    aimat->AddProperty(&mat.pbrMetallicRoughness.roughnessFactor, 1, AI_MATKEY_GLTF_PBRMETALLICROUGHNESS_ROUGHNESS_FACTOR);

    float roughnessAsShininess = 1 - mat.pbrMetallicRoughness.roughnessFactor;
    roughnessAsShininess *= roughnessAsShininess * 1000;
    aimat->AddProperty(&roughnessAsShininess, 1, AI_MATKEY_SHININESS);

    SetMaterialTextureProperty(embeddedTexIdxs, r, mat.normalTexture, aimat, aiTextureType_NORMALS);
    SetMaterialTextureProperty(embeddedTexIdxs, r, mat.occlusionTexture, aimat, aiTextureType_LIGHTMAP);
    SetMaterialTextureProperty(embeddedTexIdxs, r, mat.emissiveTexture, aimat, aiTextureType_EMISSIVE);
    SetMaterialColorProperty(r, mat.emissiveFactor, aimat, AI_MATKEY_COLOR_EMISSIVE);

    aimat->AddProperty(&mat.doubleSided, 1, AI_MATKEY_TWOSIDED);

    aiString alphaMode(mat.alphaMode);
    aimat->AddProperty(&alphaMode, AI_MATKEY_GLTF_ALPHAMODE);
    aimat->AddProperty(&mat.alphaCutoff, 1, AI_MATKEY_GLTF_ALPHACUTOFF);

    //pbrSpecularGlossiness
    if (mat.pbrSpecularGlossiness.isPresent) {
        PbrSpecularGlossiness &pbrSG = mat.pbrSpecularGlossiness.value;

        aimat->AddProperty(&mat.pbrSpecularGlossiness.isPresent, 1, AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS);
        SetMaterialColorProperty(r, pbrSG.diffuseFactor, aimat, AI_MATKEY_COLOR_DIFFUSE);
        SetMaterialColorProperty(r, pbrSG.specularFactor, aimat, AI_MATKEY_COLOR_SPECULAR);

        float glossinessAsShininess = pbrSG.glossinessFactor * 1000.0f;
        aimat->AddProperty(&glossinessAsShininess, 1, AI_MATKEY_SHININESS);
        aimat->AddProperty(&pbrSG.glossinessFactor, 1, AI_MATKEY_GLTF_PBRSPECULARGLOSSINESS_GLOSSINESS_FACTOR);

        SetMaterialTextureProperty(embeddedTexIdxs, r, pbrSG.diffuseTexture, aimat, aiTextureType_DIFFUSE);

        SetMaterialTextureProperty(embeddedTexIdxs, r, pbrSG.specularGlossinessTexture, aimat, aiTextureType_SPECULAR);
    }
    if (mat.unlit) {
        aimat->AddProperty(&mat.unlit, 1, AI_MATKEY_GLTF_UNLIT);
    }

    return aimat;
}

void glTF2Importer::ImportMaterials(glTF2::Asset& r)
{
    const unsigned int numImportedMaterials = unsigned(r.materials.Size());
    Material defaultMaterial;

    mScene->mNumMaterials = numImportedMaterials + 1;
    mScene->mMaterials = new aiMaterial*[mScene->mNumMaterials];
    mScene->mMaterials[numImportedMaterials] = ImportMaterial(embeddedTexIdxs, r, defaultMaterial);

    for (unsigned int i = 0; i < numImportedMaterials; ++i) {
       mScene->mMaterials[i] = ImportMaterial(embeddedTexIdxs, r, r.materials[i]);
    }
}


static inline void SetFace(aiFace& face, int a)
{
    face.mNumIndices = 1;
    face.mIndices = new unsigned int[1];
    face.mIndices[0] = a;
}

static inline void SetFace(aiFace& face, int a, int b)
{
    face.mNumIndices = 2;
    face.mIndices = new unsigned int[2];
    face.mIndices[0] = a;
    face.mIndices[1] = b;
}

static inline void SetFace(aiFace& face, int a, int b, int c)
{
    face.mNumIndices = 3;
    face.mIndices = new unsigned int[3];
    face.mIndices[0] = a;
    face.mIndices[1] = b;
    face.mIndices[2] = c;
}

#ifdef ASSIMP_BUILD_DEBUG
static inline bool CheckValidFacesIndices(aiFace* faces, unsigned nFaces, unsigned nVerts)
{
    for (unsigned i = 0; i < nFaces; ++i) {
        for (unsigned j = 0; j < faces[i].mNumIndices; ++j) {
            unsigned idx = faces[i].mIndices[j];
            if (idx >= nVerts)
                return false;
        }
    }
    return true;
}
#endif // ASSIMP_BUILD_DEBUG

typedef aiMesh* MeshPtr;

void glTF2Importer::ImportMeshes(glTF2::Asset& r)
{
    std::vector<aiMesh*> meshes;

    unsigned int k = 0;

    for (unsigned int m = 0; m < r.meshes.Size(); ++m) {
        Mesh& mesh = r.meshes[m];

        meshOffsets.push_back(k);
        k += unsigned(mesh.primitives.size());

        for (unsigned int p = 0; p < mesh.primitives.size(); ++p) {
            Mesh::Primitive& prim = mesh.primitives[p];

            aiMesh* aim = new aiMesh();
            meshes.push_back(aim);

            aim->mName = mesh.name.empty() ? mesh.id : mesh.name;

            if (mesh.primitives.size() > 1) {
                size_t& len = aim->mName.length;
                aim->mName.data[len] = '-';
                len += 1 + ASSIMP_itoa10(aim->mName.data + len + 1, unsigned(MAXLEN - len - 1), p);
            }

            switch (prim.mode) {
                case PrimitiveMode_POINTS:
                    aim->mPrimitiveTypes |= aiPrimitiveType_POINT;
                    break;

                case PrimitiveMode_LINES:
                case PrimitiveMode_LINE_LOOP:
                case PrimitiveMode_LINE_STRIP:
                    aim->mPrimitiveTypes |= aiPrimitiveType_LINE;
                    break;

                case PrimitiveMode_TRIANGLES:
                case PrimitiveMode_TRIANGLE_STRIP:
                case PrimitiveMode_TRIANGLE_FAN:
                    aim->mPrimitiveTypes |= aiPrimitiveType_TRIANGLE;
                    break;

            }

            Mesh::Primitive::Attributes& attr = prim.attributes;

            if (attr.position.size() > 0 && attr.position[0]) {
                aim->mNumVertices = attr.position[0]->count;
                attr.position[0]->ExtractData(aim->mVertices);
            }

            if (attr.normal.size() > 0 && attr.normal[0]) {
                attr.normal[0]->ExtractData(aim->mNormals);

                // only extract tangents if normals are present
                if (attr.tangent.size() > 0 && attr.tangent[0]) {
                    // generate bitangents from normals and tangents according to spec
                    Tangent *tangents = nullptr;

                    attr.tangent[0]->ExtractData(tangents);

                    aim->mTangents = new aiVector3D[aim->mNumVertices];
                    aim->mBitangents = new aiVector3D[aim->mNumVertices];

                    for (unsigned int i = 0; i < aim->mNumVertices; ++i) {
                        aim->mTangents[i] = tangents[i].xyz;
                        aim->mBitangents[i] = (aim->mNormals[i] ^ tangents[i].xyz) * tangents[i].w;
                    }

                    delete [] tangents;
                }
            }

            for (size_t c = 0; c < attr.color.size() && c < AI_MAX_NUMBER_OF_COLOR_SETS; ++c) {
                if (attr.color[c]->count != aim->mNumVertices) {
                    DefaultLogger::get()->warn("Color stream size in mesh \"" + mesh.name +
                        "\" does not match the vertex count");
                    continue;
                }
                aim->mColors[c] = new aiColor4D[attr.color[c]->count];
                attr.color[c]->ExtractData(aim->mColors[c]);
            }
            for (size_t tc = 0; tc < attr.texcoord.size() && tc < AI_MAX_NUMBER_OF_TEXTURECOORDS; ++tc) {
                if (attr.texcoord[tc]->count != aim->mNumVertices) {
                    DefaultLogger::get()->warn("Texcoord stream size in mesh \"" + mesh.name +
                                               "\" does not match the vertex count");
                    continue;
                }

                attr.texcoord[tc]->ExtractData(aim->mTextureCoords[tc]);
                aim->mNumUVComponents[tc] = attr.texcoord[tc]->GetNumComponents();

                aiVector3D* values = aim->mTextureCoords[tc];
                for (unsigned int i = 0; i < aim->mNumVertices; ++i) {
                    values[i].y = 1 - values[i].y; // Flip Y coords
                }
            }
            std::vector<Mesh::Primitive::Target>& targets = prim.targets;
            unsigned int numTargets = targets.size();
            if (numTargets > 0) {
                aim->mNumAnimMeshes = numTargets;
                aim->mAnimMeshes = new aiAnimMesh*[numTargets];
                for (size_t i = 0; i < numTargets; i++) {
                    aim->mAnimMeshes[i] = aiCreateAnimMesh(aim);
                    aiAnimMesh& aiAnimMesh = *(aim->mAnimMeshes[i]);
                    Mesh::Primitive::Target& target = targets[i];

                    if (target.position.size() > 0) {
                        aiVector3D *positionDiff = nullptr;
                        target.position[0]->ExtractData(positionDiff);
                        for(unsigned int vertexId = 0; vertexId < aim->mNumVertices; vertexId++) {
                            aiAnimMesh.mVertices[vertexId] += positionDiff[vertexId];
                        }
                        delete [] positionDiff;
                    }
                    if (target.normal.size() > 0) {
                        aiVector3D *normalDiff = nullptr;
                        target.normal[0]->ExtractData(normalDiff);
                        for(unsigned int vertexId = 0; vertexId < aim->mNumVertices; vertexId++) {
                            aiAnimMesh.mNormals[vertexId] += normalDiff[vertexId];
                        }
                        delete [] normalDiff;
                    }
                    if (target.tangent.size() > 0) {
                        Tangent *tangent = nullptr;
                        attr.tangent[0]->ExtractData(tangent);

                        aiVector3D *tangentDiff = nullptr;
                        target.tangent[0]->ExtractData(tangentDiff);

                        for (unsigned int vertexId = 0; vertexId < aim->mNumVertices; ++vertexId) {
                            tangent[vertexId].xyz += tangentDiff[vertexId];
                            aiAnimMesh.mTangents[vertexId] = tangent[vertexId].xyz;
                            aiAnimMesh.mBitangents[vertexId] = (aiAnimMesh.mNormals[vertexId] ^ tangent[vertexId].xyz) * tangent[vertexId].w;
                        }
                        delete [] tangent;
                        delete [] tangentDiff;
                    }
                    if (mesh.weights.size() > i) {
                        aiAnimMesh.mWeight = mesh.weights[i];
                    }
                }
            }

            aiFace* faces = 0;
            unsigned int nFaces = 0;

            if (prim.indices) {
                unsigned int count = prim.indices->count;

                Accessor::Indexer data = prim.indices->GetIndexer();
                ai_assert(data.IsValid());

                switch (prim.mode) {
                    case PrimitiveMode_POINTS: {
                        nFaces = count;
                        faces = new aiFace[nFaces];
                        for (unsigned int i = 0; i < count; ++i) {
                            SetFace(faces[i], data.GetUInt(i));
                        }
                        break;
                    }

                    case PrimitiveMode_LINES: {
                        nFaces = count / 2;
                        faces = new aiFace[nFaces];
                        for (unsigned int i = 0; i < count; i += 2) {
                            SetFace(faces[i / 2], data.GetUInt(i), data.GetUInt(i + 1));
                        }
                        break;
                    }

                    case PrimitiveMode_LINE_LOOP:
                    case PrimitiveMode_LINE_STRIP: {
                        nFaces = count - ((prim.mode == PrimitiveMode_LINE_STRIP) ? 1 : 0);
                        faces = new aiFace[nFaces];
                        SetFace(faces[0], data.GetUInt(0), data.GetUInt(1));
                        for (unsigned int i = 2; i < count; ++i) {
                            SetFace(faces[i - 1], faces[i - 2].mIndices[1], data.GetUInt(i));
                        }
                        if (prim.mode == PrimitiveMode_LINE_LOOP) { // close the loop
                            SetFace(faces[count - 1], faces[count - 2].mIndices[1], faces[0].mIndices[0]);
                        }
                        break;
                    }

                    case PrimitiveMode_TRIANGLES: {
                        nFaces = count / 3;
                        faces = new aiFace[nFaces];
                        for (unsigned int i = 0; i < count; i += 3) {
                            SetFace(faces[i / 3], data.GetUInt(i), data.GetUInt(i + 1), data.GetUInt(i + 2));
                        }
                        break;
                    }
                    case PrimitiveMode_TRIANGLE_STRIP: {
                        nFaces = count - 2;
                        faces = new aiFace[nFaces];
                        for (unsigned int i = 0; i < nFaces; ++i) {
                            //The ordering is to ensure that the triangles are all drawn with the same orientation
                            if ((i + 1) % 2 == 0)
                            {
                                //For even n, vertices n + 1, n, and n + 2 define triangle n
                                SetFace(faces[i], data.GetUInt(i + 1), data.GetUInt(i), data.GetUInt(i + 2));
                            }
                            else
                            {
                                //For odd n, vertices n, n+1, and n+2 define triangle n
                                SetFace(faces[i], data.GetUInt(i), data.GetUInt(i + 1), data.GetUInt(i + 2));
                            }
                        }
                        break;
                    }
                    case PrimitiveMode_TRIANGLE_FAN:
                        nFaces = count - 2;
                        faces = new aiFace[nFaces];
                        SetFace(faces[0], data.GetUInt(0), data.GetUInt(1), data.GetUInt(2));
                        for (unsigned int i = 1; i < nFaces; ++i) {
                            SetFace(faces[i], faces[0].mIndices[0], faces[i - 1].mIndices[2], data.GetUInt(i + 2));
                        }
                        break;
                }
            }
            else { // no indices provided so directly generate from counts

                // use the already determined count as it includes checks
                unsigned int count = aim->mNumVertices;

                switch (prim.mode) {
                case PrimitiveMode_POINTS: {
                    nFaces = count;
                    faces = new aiFace[nFaces];
                    for (unsigned int i = 0; i < count; ++i) {
                        SetFace(faces[i], i);
                    }
                    break;
                }

                case PrimitiveMode_LINES: {
                    nFaces = count / 2;
                    faces = new aiFace[nFaces];
                    for (unsigned int i = 0; i < count; i += 2) {
                        SetFace(faces[i / 2], i, i + 1);
                    }
                    break;
                }

                case PrimitiveMode_LINE_LOOP:
                case PrimitiveMode_LINE_STRIP: {
                    nFaces = count - ((prim.mode == PrimitiveMode_LINE_STRIP) ? 1 : 0);
                    faces = new aiFace[nFaces];
                    SetFace(faces[0], 0, 1);
                    for (unsigned int i = 2; i < count; ++i) {
                        SetFace(faces[i - 1], faces[i - 2].mIndices[1], i);
                    }
                    if (prim.mode == PrimitiveMode_LINE_LOOP) { // close the loop
                        SetFace(faces[count - 1], faces[count - 2].mIndices[1], faces[0].mIndices[0]);
                    }
                    break;
                }

                case PrimitiveMode_TRIANGLES: {
                    nFaces = count / 3;
                    faces = new aiFace[nFaces];
                    for (unsigned int i = 0; i < count; i += 3) {
                        SetFace(faces[i / 3], i, i + 1, i + 2);
                    }
                    break;
                }
                case PrimitiveMode_TRIANGLE_STRIP: {
                    nFaces = count - 2;
                    faces = new aiFace[nFaces];
                    for (unsigned int i = 0; i < nFaces; ++i) {
                        //The ordering is to ensure that the triangles are all drawn with the same orientation
                        if ((i+1) % 2 == 0)
                        {
                            //For even n, vertices n + 1, n, and n + 2 define triangle n
                            SetFace(faces[i], i+1, i, i+2);
                        }
                        else
                        {
                            //For odd n, vertices n, n+1, and n+2 define triangle n
                            SetFace(faces[i], i, i+1, i+2);
                        }
                    }
                    break;
                }
                case PrimitiveMode_TRIANGLE_FAN:
                    nFaces = count - 2;
                    faces = new aiFace[nFaces];
                    SetFace(faces[0], 0, 1, 2);
                    for (unsigned int i = 1; i < nFaces; ++i) {
                        SetFace(faces[i], faces[0].mIndices[0], faces[i - 1].mIndices[2], i + 2);
                    }
                    break;
                }
            }

            if (faces) {
                aim->mFaces = faces;
                aim->mNumFaces = nFaces;
                ai_assert(CheckValidFacesIndices(faces, nFaces, aim->mNumVertices));
            }

            if (prim.material) {
                aim->mMaterialIndex = prim.material.GetIndex();
            }
            else {
                aim->mMaterialIndex = mScene->mNumMaterials - 1;
            }

        }
    }

    meshOffsets.push_back(k);
    MeshPtr* meshptrs = new MeshPtr[meshes.size()];
    int i = 0;
    for (auto iter = meshes.begin(); iter != meshes.end(); ++iter)
    {
        meshptrs[i++] = *iter;
    }
    mScene->mMeshes = meshptrs;
    mScene->mNumMeshes = i;
    //CopyVector(meshes, mScene->mMeshes, mScene->mNumMeshes);
}

void glTF2Importer::ImportCameras(glTF2::Asset& r)
{
    if (!r.cameras.Size()) return;

    mScene->mNumCameras = r.cameras.Size();
    mScene->mCameras = new aiCamera*[r.cameras.Size()];

    for (size_t i = 0; i < r.cameras.Size(); ++i) {
        Camera& cam = r.cameras[i];

        aiCamera* aicam = mScene->mCameras[i] = new aiCamera();

        // cameras point in -Z by default, rest is specified in node transform
        aicam->mLookAt = aiVector3D(0.f,0.f,-1.f);

        if (cam.type == Camera::Perspective) {

            aicam->mAspect        = cam.cameraProperties.perspective.aspectRatio;
            aicam->mHorizontalFOV = cam.cameraProperties.perspective.yfov * aicam->mAspect;
            aicam->mClipPlaneFar  = cam.cameraProperties.perspective.zfar;
            aicam->mClipPlaneNear = cam.cameraProperties.perspective.znear;
        }
        else {
            // assimp does not support orthographic cameras
        }
    }
}

static void GetNodeTransform(aiMatrix4x4& matrix, const glTF2::Node& node) {
    if (node.matrix.isPresent) {
        CopyValue(node.matrix.value, matrix);
    }
    else {
        if (node.translation.isPresent) {
            aiVector3D trans;
            CopyValue(node.translation.value, trans);
            aiMatrix4x4 t;
            aiMatrix4x4::Translation(trans, t);
            matrix = matrix * t;
        }

        if (node.rotation.isPresent) {
            aiQuaternion rot;
            CopyValue(node.rotation.value, rot);
            matrix = matrix * aiMatrix4x4(rot.GetMatrix());
        }

        if (node.scale.isPresent) {
            aiVector3D scal(1.f);
            CopyValue(node.scale.value, scal);
            aiMatrix4x4 s;
            aiMatrix4x4::Scaling(scal, s);
            matrix = matrix * s;
        }
    }
}

static void BuildVertexWeightMapping(Mesh::Primitive& primitive, std::vector<std::vector<aiVertexWeight>>& map)
{
    Mesh::Primitive::Attributes& attr = primitive.attributes;
    if (attr.weight.empty() || attr.joint.empty()) {
        return;
    }
    if (attr.weight[0]->count != attr.joint[0]->count) {
        return;
    }

    const int num_vertices = attr.weight[0]->count;

    struct Weights { float values[4]; };
    Weights* weights = nullptr;
    attr.weight[0]->ExtractData(weights);

    struct Indices8 { uint8_t values[4]; };
    struct Indices16 { uint16_t values[4]; };
    Indices8* indices8 = nullptr;
    Indices16* indices16 = nullptr;
    if (attr.joint[0]->GetElementSize() == 4) {
        attr.joint[0]->ExtractData(indices8);
    }else {
        attr.joint[0]->ExtractData(indices16);
    }
    // 
    if (nullptr == indices8 && nullptr == indices16) {
        // Something went completely wrong!
        ai_assert(false);
        return;
    }

    for (int i = 0; i < num_vertices; ++i) {
        for (int j = 0; j < 4; ++j) {
            const unsigned int bone = (indices8!=nullptr) ? indices8[i].values[j] : indices16[i].values[j];
            const float weight = weights[i].values[j];
            if (weight > 0 && bone < map.size()) {
                map[bone].reserve(8);
                map[bone].emplace_back(i, weight);
            }
        }
    }

    delete[] weights;
    delete[] indices8;
    delete[] indices16;
}

aiNode* ImportNode(aiScene* pScene, glTF2::Asset& r, std::vector<unsigned int>& meshOffsets, glTF2::Ref<glTF2::Node>& ptr)
{
    Node& node = *ptr;
    std::string nameOrId = node.name.empty() ? node.id : node.name;
    aiNode* ainode = new aiNode(nameOrId);

    ainode->mNumChildren = 0;
    if (!node.children.empty()) {
        ainode->mNumChildren = unsigned(node.children.size());
        ainode->mChildren = new aiNode*[ainode->mNumChildren];

        for (unsigned int i = 0; i < ainode->mNumChildren; ++i) {
            aiNode* child = ImportNode(pScene, r, meshOffsets, node.children[i]);
            child->mParent = ainode;
            ainode->mChildren[i] = child;
        }
    }

    GetNodeTransform(ainode->mTransformation, node);
    if (!node.meshes.empty())
    {
        int count = 0;
        for (size_t i = 0; i < node.meshes.size(); ++i)
        {
            int idx = node.meshes[i].GetIndex();
            count += meshOffsets[idx + 1] - meshOffsets[idx];
        }
        ainode->mNumMeshes = count;
        ainode->mMeshes = new unsigned int[count];

        int k = 0;
        for (size_t i = 0; i < node.meshes.size(); ++i)
        {
            int idx = node.meshes[i].GetIndex();
            for (unsigned int j = meshOffsets[idx]; j < meshOffsets[idx + 1]; ++j, ++k)
            {
                ainode->mMeshes[k] = j;
            }
        }

#ifdef GVRF_ASSIMP
        Mesh &mesh = *(node.meshes[0]);
        if (node.skin)
        {
            int totalBones = node.skin->jointNames.size();

            //nodes which represent bones for this node
            std::vector<Ref<Node>> boneNodes = node.skin->jointNames;

            //get the inverse bind matrices
            aiMatrix4x4 *ibms = new aiMatrix4x4[totalBones];
            node.skin->inverseBindMatrices->ExtractData(ibms);


            //hash to determine the bones used by a mesh primitive
            std::vector<bool> boneSet(totalBones, false);

            for (unsigned int i = 0; i < mesh.primitives.size(); ++i)
            {

                //2d vector of bones. each bone in this vector has a vector of aiVertexWeights
                //each aiVertexWeight contains a pair of vertex index and the weight associated to this bone.
                std::vector<std::vector<aiVertexWeight>> boneVec(totalBones,
                                                                 std::vector<aiVertexWeight>());

                //extract joints and weights and vertex indices fromt the mesh data
                Joint *jointAttr = nullptr;
                Weight *weightAttr = nullptr;

                Mesh::Primitive &prim = mesh.primitives[i];
                Mesh::Primitive::Attributes &attr = prim.attributes;

                if (attr.joint.size() > 0 && attr.joint[0])
                    attr.joint[0]->ExtractData(jointAttr);

                if (attr.weight.size() > 0 && attr.weight[0])
                    attr.weight[0]->ExtractData(weightAttr);

                unsigned int numBones = 0;

                //for every bone, get all the vertex indices affected by it
                //and the weight of that bone for that vertex.
                for (unsigned int k = 0; k < attr.joint[0]->count; ++k)
                {
                    for (unsigned int l = 0; l < 4; ++l)
                    {
                        if (weightAttr[k].weightinfo[l] > 0.001)
                        {
                            unsigned int boneIdx = static_cast<int>(jointAttr[k].jointinfo[l]);

                            if (!boneSet[boneIdx])
                            {
                                numBones++;
                                boneSet[boneIdx] = true;
                            }

                            aiVertexWeight vw(k, weightAttr[k].weightinfo[l]);
                            boneVec[boneIdx].push_back(vw);
                        }
                    }
                }

                aiBone **bones = new aiBone *[numBones];

                for (unsigned int j = 0; j < numBones; ++j)
                    bones[j] = new aiBone();

                unsigned int itr = 0;
                for (unsigned int j = 0; j < totalBones; ++j)
                {
                    if (boneSet[j] == true)
                    {
                        //set the bone name as the node name
                        bones[itr]->mName = boneNodes[j]->name.empty() ? boneNodes[j]->id : boneNodes[j]->name;
                        //set the inverse bind matrix
                        bones[itr]->mOffsetMatrix = ibms[j].Inverse();
                        //set the vertex index+weight array
                        aiVertexWeight *vw = new aiVertexWeight[boneVec[j].size()];
                        for (unsigned int l = 0; l < boneVec[j].size(); ++l)
                            vw[l] = boneVec[j][l];

                        bones[itr]->mNumWeights = boneVec[j].size();
                        bones[itr]->mWeights = vw;
                        ++itr;
                    }
                }
                int meshid = ainode->mMeshes[i];
                aiMesh *m = pScene->mMeshes[meshid];
                m->mBones = bones;
                m->mNumBones = numBones;

                // clear/delete structures
                for (unsigned int k = 0; k < totalBones; ++k)
                    boneVec[k].clear();
                boneVec.clear();

                delete jointAttr;
                delete weightAttr;

                //reset hash of bones referred by the primitive
                std::fill(boneSet.begin(), boneSet.end(), false);
            }

            delete[] ibms;
            boneNodes.clear();
        }
#else
        if (node.skin)
        {
            for (int primitiveNo = 0; primitiveNo < count; ++primitiveNo)
            {
                aiMesh *mesh = pScene->mMeshes[meshOffsets[mesh_idx] + primitiveNo];
                mesh->mNumBones = node.skin->jointNames.size();
                mesh->mBones = new aiBone *[mesh->mNumBones];

                // GLTF and Assimp choose to store bone weights differently.
                // GLTF has each vertex specify which bones influence the vertex.
                // Assimp has each bone specify which vertices it has influence over.
                // To convert this data, we first read over the vertex data and pull
                // out the bone-to-vertex mapping.  Then, when creating the aiBones,
                // we copy the bone-to-vertex mapping into the bone.  This is unfortunate
                // both because it's somewhat slow and because, for many applications,
                // we then need to reconvert the data back into the vertex-to-bone
                // mapping which makes things doubly-slow.
                std::vector<std::vector<aiVertexWeight>> weighting(mesh->mNumBones);
                BuildVertexWeightMapping(node.meshes[0]->primitives[primitiveNo], weighting);

                for (size_t i = 0; i < mesh->mNumBones; ++i)
                {
                    aiBone *bone = new aiBone();

                    Ref<Node> joint = node.skin->jointNames[i];
                    if (!joint->name.empty())
                    {
                        bone->mName = joint->name;
                    }
                    else
                    {
                        // Assimp expects each bone to have a unique name.
                        static const std::string kDefaultName = "bone_";
                        char postfix[10] = {0};
                        ASSIMP_itoa10(postfix, i);
                        bone->mName = (kDefaultName + postfix);
                    }
                    GetNodeTransform(bone->mOffsetMatrix, *joint);

                    std::vector<aiVertexWeight> &weights = weighting[i];

                    bone->mNumWeights = weights.size();
                    if (bone->mNumWeights > 0)
                    {
                        bone->mWeights = new aiVertexWeight[bone->mNumWeights];
                        memcpy(bone->mWeights, weights.data(),
                               bone->mNumWeights * sizeof(aiVertexWeight));
                    }
                    else
                    {
                        // Assimp expects all bones to have at least 1 weight.
                        bone->mWeights = new aiVertexWeight[1];
                        bone->mNumWeights = 1;
                        bone->mWeights->mVertexId = 0;
                        bone->mWeights->mWeight = 0.f;
                    }
                    mesh->mBones[i] = bone;
                }
            }
        }
        int k = 0;
        for (unsigned int j = meshOffsets[mesh_idx]; j < meshOffsets[mesh_idx + 1]; ++j, ++k)
        {
            ainode->mMeshes[k] = j;
        }
#endif
    }
    if (node.camera) {
        pScene->mCameras[node.camera.GetIndex()]->mName = ainode->mName;
    }

    return ainode;
}

void glTF2Importer::ImportNodes(glTF2::Asset& r)
{
    if (!r.scene) return;

    std::vector< Ref<Node> > rootNodes = r.scene->nodes;

    // The root nodes
    unsigned int numRootNodes = unsigned(rootNodes.size());
    if (numRootNodes == 1) { // a single root node: use it
        mScene->mRootNode = ImportNode(mScene, r, meshOffsets, rootNodes[0]);
    }
    else if (numRootNodes > 1) { // more than one root node: create a fake root
        aiNode* root = new aiNode("ROOT");
        root->mChildren = new aiNode*[numRootNodes];
        for (unsigned int i = 0; i < numRootNodes; ++i) {
            aiNode* node = ImportNode(mScene, r, meshOffsets, rootNodes[i]);
            node->mParent = root;
            root->mChildren[root->mNumChildren++] = node;
        }
        mScene->mRootNode = root;
    }

    //if (!mScene->mRootNode) {
    //  mScene->mRootNode = new aiNode("EMPTY");
    //}
}

#ifdef GVRF_ASSIMP
void glTF2Importer::ImportAnimations(glTF2::Asset& r)
{
    std::vector<aiAnimation *> anims;
    anims.resize(r.animations.Size());
    mScene->mNumAnimations = r.animations.Size();
    int numNodeAnimChannels = 0; int numMorphAnimChannels = 0;
    double animDuration = 0.0;

    for(size_t i = 0; i < r.animations.Size(); i ++ )
    {
        Animation animRead = r.animations[i];

        std::vector<aiNodeAnim *> animChannels;
        std::vector<aiMeshMorphAnim *> meshAnimChannels;

        for(size_t j = 0; j < animRead.Channels.size(); j ++ )
        {
            aiNodeAnim * aiChannel;
            aiMeshMorphAnim * aiMorphChannel;
            int numMorphTargets = 0;


            Animation::AnimChannel channelRead = animRead.Channels[j];
            Animation::AnimSampler samplerRead = animRead.Samplers[channelRead.sampler];

            //get the time stamps
            Accessor::Indexer timeStamps = samplerRead.TIME->GetIndexer();
            ai_assert(timeStamps.IsValid());

            Animation::AnimChannel::AnimTarget targetRead = channelRead.target;
            std::string keyType = targetRead.path;
            int keyCount = samplerRead.TIME->count;

            aiVector3D * positionKeys = nullptr;
            aiVector3D * scaleKeys = nullptr;
            aiQuaternion * rotKeys = nullptr;
            float * blendkeys = nullptr;


            //based on keytype extract data from accessor

            if(keyType == "weights")
            {
                aiMorphChannel = new aiMeshMorphAnim();
                aiMorphChannel->mNumKeys = keyCount;
                aiMorphChannel->mKeys = new aiMeshMorphKey[keyCount];
                numMorphAnimChannels++;
                numMorphTargets = samplerRead.output->count / keyCount;
                samplerRead.output->ExtractData(blendkeys);
            }
            else
            {
                aiChannel = new aiNodeAnim();
                numNodeAnimChannels++;

                if(keyType == "translation")
                {
                    aiChannel->mPositionKeys = new aiVectorKey[keyCount];
                    aiChannel->mNumPositionKeys = keyCount;
                    samplerRead.output->ExtractData(positionKeys);
                }
                else if(keyType == "rotation")
                {
                    aiChannel->mRotationKeys = new aiQuatKey[keyCount];
                    aiChannel->mNumRotationKeys = keyCount;
                    samplerRead.output->ExtractData(rotKeys);

                }
                else if(keyType == "scale")
                {
                    aiChannel->mScalingKeys = new aiVectorKey[keyCount];
                    aiChannel->mNumScalingKeys = keyCount;
                    samplerRead.output->ExtractData(scaleKeys);
                }
            }


            //update channels based on input/output data
            double firstTimeStamp = 0;
            for(size_t k = 0; k < keyCount; k ++ )
            {
                double currTimeStamp = timeStamps.GetValue<float>(k);
                if(k == 0)
                    firstTimeStamp = currTimeStamp;

                if(k == keyCount - 1)
                    animDuration = std::max(currTimeStamp - firstTimeStamp, animDuration);

                if(keyType == "translation")
                {
                    aiChannel->mPositionKeys[k].mTime = currTimeStamp;
                    aiChannel->mPositionKeys[k].mValue = positionKeys[k];

                }
                else if(keyType == "scale")
                {
                    aiChannel->mScalingKeys[k].mTime = currTimeStamp;
                    aiChannel->mScalingKeys[k].mValue = scaleKeys[k];
                }
                else if(keyType == "rotation")
                {
                    aiChannel->mRotationKeys[k].mTime = currTimeStamp;
                    aiChannel->mRotationKeys[k].mValue = rotKeys[k];

                }


                /**
                 * Extracted buffer is an array of floats. We need to segment
                 * every 'numMorphTargets' floats. This segment is out key for
                 * the channel.
                 **/

                else if(keyType == "weights")
                {

                    aiMorphChannel->mKeys[k].mTime = currTimeStamp;

                    double * blendWeights = new double[numMorphTargets];
                    for(int itr = 0; itr < numMorphTargets ; itr ++ )
                        blendWeights[itr] = blendkeys[itr + numMorphTargets * k];
                    aiMorphChannel->mKeys[k].mWeights = blendWeights;
                    aiMorphChannel->mKeys[k].mNumValuesAndWeights = numMorphTargets;
                }
            }


            if(keyType == "weights")
            {
                aiMorphChannel->mName = channelRead.target.node->name.empty() ? channelRead.target.node->id : channelRead.target.node->name;
                meshAnimChannels.push_back(aiMorphChannel);
            }
            else
            {
                aiChannel->mNodeName = channelRead.target.node->name.empty() ? channelRead.target.node->id : channelRead.target.node->name;
                aiChannel->mPreState = aiAnimBehaviour_DEFAULT;
                aiChannel->mPostState = aiAnimBehaviour_DEFAULT;

                animChannels.push_back(aiChannel);
            }


            delete positionKeys;
            delete scaleKeys;
            delete rotKeys;
            delete blendkeys;

        }

        //condense all channels belonging to one node into one channel


        //using comparator for map which doesnt care about ordering.
        //should replace by unordered_map when using c++11.
        std::map< aiString, aiNodeAnim* , mycomp> uniqueNodes;

        for(size_t j = 0; j < animChannels.size(); ++j)
        {
            aiString currNodeName = animChannels[j]->mNodeName;
            std::map< aiString, aiNodeAnim* >::iterator it = uniqueNodes.find(currNodeName);

            if(it==uniqueNodes.end()) {
                uniqueNodes.insert(std::pair< aiString, aiNodeAnim* >(currNodeName, animChannels[j]));
            }
            else{
                if(animChannels[j]->mNumPositionKeys > 0)
                {
                    it->second->mPositionKeys = animChannels[j]->mPositionKeys;
                    it->second->mNumPositionKeys = animChannels[j]->mNumPositionKeys;
                }
                else if(animChannels[j]->mNumScalingKeys > 0)
                {
                    it->second->mScalingKeys = animChannels[j]->mScalingKeys;
                    it->second->mNumScalingKeys = animChannels[j]->mNumScalingKeys;
                }
                else if(animChannels[j]->mNumRotationKeys > 0)
                {
                    it->second->mRotationKeys = animChannels[j]->mRotationKeys;
                    it->second->mNumRotationKeys = animChannels[j]->mNumRotationKeys;
                }
            }
        }


        anims[i] = new aiAnimation();
        anims[i]->mDuration = animDuration;
        anims[i]->mNumChannels = uniqueNodes.size();
        anims[i]->mNumMorphMeshChannels = numMorphAnimChannels;

        aiNodeAnim **channels = new aiNodeAnim* [numNodeAnimChannels];
        aiMeshMorphAnim ** morphChannels = new aiMeshMorphAnim* [numMorphAnimChannels];

        // for(int itr = 0; itr < anims[i]->mNumChannels; ++itr)
        //     channels[itr] = animChannels[itr];

        int itr = 0;
        for(std::map<aiString, aiNodeAnim*>::iterator it = uniqueNodes.begin() ; it != uniqueNodes.end(); ++it)
        {
            channels[itr] = it->second;
            ++itr;
        }

        for(int itr = 0; itr < anims[i]->mNumMorphMeshChannels; ++itr)
            morphChannels[itr] = meshAnimChannels[itr];


        anims[i]->mChannels = channels;
        anims[i]->mMorphMeshChannels = morphChannels;

        anims[i]->mTicksPerSecond = 1.0;

        animChannels.clear();
        meshAnimChannels.clear();
        uniqueNodes.clear();
    }

    aiAnimation ** animations = new aiAnimation* [mScene->mNumAnimations];
    for(int itr = 0; itr < mScene->mNumAnimations; ++itr)
        animations[itr] = anims[itr];

    mScene->mAnimations = animations;

    anims.clear();

}


#else
struct AnimationSamplers {
    AnimationSamplers() : translation(nullptr), rotation(nullptr), scale(nullptr) {}

    Animation::Sampler* translation;
    Animation::Sampler* rotation;
    Animation::Sampler* scale;
};

aiNodeAnim* CreateNodeAnim(glTF2::Asset& r, Node& node, AnimationSamplers& samplers)
{
    aiNodeAnim* anim = new aiNodeAnim();
    anim->mNodeName = node.name;

    static const float kMillisecondsFromSeconds = 1000.f;

    if (samplers.translation) {
        float* times = nullptr;
        samplers.translation->input->ExtractData(times);
        aiVector3D* values = nullptr;
        samplers.translation->output->ExtractData(values);
        anim->mNumPositionKeys = samplers.translation->input->count;
        anim->mPositionKeys = new aiVectorKey[anim->mNumPositionKeys];
        for (unsigned int i = 0; i < anim->mNumPositionKeys; ++i) {
            anim->mPositionKeys[i].mTime = times[i] * kMillisecondsFromSeconds;
            anim->mPositionKeys[i].mValue = values[i];
        }
        delete[] times;
        delete[] values;
    } else if (node.translation.isPresent) {
        anim->mNumPositionKeys = 1;
        anim->mPositionKeys = new aiVectorKey();
        anim->mPositionKeys->mTime = 0.f;
        anim->mPositionKeys->mValue.x = node.translation.value[0];
        anim->mPositionKeys->mValue.y = node.translation.value[1];
        anim->mPositionKeys->mValue.z = node.translation.value[2];
    }

    if (samplers.rotation) {
        float* times = nullptr;
        samplers.rotation->input->ExtractData(times);
        aiQuaternion* values = nullptr;
        samplers.rotation->output->ExtractData(values);
        anim->mNumRotationKeys = samplers.rotation->input->count;
        anim->mRotationKeys = new aiQuatKey[anim->mNumRotationKeys];
        for (unsigned int i = 0; i < anim->mNumRotationKeys; ++i) {
            anim->mRotationKeys[i].mTime = times[i] * kMillisecondsFromSeconds;
            anim->mRotationKeys[i].mValue.x = values[i].w;
            anim->mRotationKeys[i].mValue.y = values[i].x;
            anim->mRotationKeys[i].mValue.z = values[i].y;
            anim->mRotationKeys[i].mValue.w = values[i].z;
        }
        delete[] times;
        delete[] values;
    } else if (node.rotation.isPresent) {
        anim->mNumRotationKeys = 1;
        anim->mRotationKeys = new aiQuatKey();
        anim->mRotationKeys->mTime = 0.f;
        anim->mRotationKeys->mValue.x = node.rotation.value[0];
        anim->mRotationKeys->mValue.y = node.rotation.value[1];
        anim->mRotationKeys->mValue.z = node.rotation.value[2];
        anim->mRotationKeys->mValue.w = node.rotation.value[3];
    }

    if (samplers.scale) {
        float* times = nullptr;
        samplers.scale->input->ExtractData(times);
        aiVector3D* values = nullptr;
        samplers.scale->output->ExtractData(values);
        anim->mNumScalingKeys = samplers.scale->input->count;
        anim->mScalingKeys = new aiVectorKey[anim->mNumScalingKeys];
        for (unsigned int i = 0; i < anim->mNumScalingKeys; ++i) {
            anim->mScalingKeys[i].mTime = times[i] * kMillisecondsFromSeconds;
            anim->mScalingKeys[i].mValue = values[i];
        }
        delete[] times;
        delete[] values;
    } else if (node.scale.isPresent) {
        anim->mNumScalingKeys = 1;
        anim->mScalingKeys = new aiVectorKey();
        anim->mScalingKeys->mTime = 0.f;
        anim->mScalingKeys->mValue.x = node.scale.value[0];
        anim->mScalingKeys->mValue.y = node.scale.value[1];
        anim->mScalingKeys->mValue.z = node.scale.value[2];
    }

    return anim;
}

std::unordered_map<unsigned int, AnimationSamplers> GatherSamplers(Animation& anim)
{
    std::unordered_map<unsigned int, AnimationSamplers> samplers;
    for (unsigned int c = 0; c < anim.channels.size(); ++c) {
        Animation::Channel& channel = anim.channels[c];
        if (channel.sampler >= static_cast<int>(anim.samplers.size())) {
            continue;
        }

        const unsigned int node_index = channel.target.node.GetIndex();

        AnimationSamplers& sampler = samplers[node_index];
        if (channel.target.path == AnimationPath_TRANSLATION) {
            sampler.translation = &anim.samplers[channel.sampler];
        } else if (channel.target.path == AnimationPath_ROTATION) {
            sampler.rotation = &anim.samplers[channel.sampler];
        } else if (channel.target.path == AnimationPath_SCALE) {
            sampler.scale = &anim.samplers[channel.sampler];
        }
    }

    return samplers;
}

void glTF2Importer::ImportAnimations(glTF2::Asset& r)
{
    if (!r.scene) return;

    mScene->mNumAnimations = r.animations.Size();
    if (mScene->mNumAnimations == 0) {
        return;
    }

    mScene->mAnimations = new aiAnimation*[mScene->mNumAnimations];
    for (unsigned int i = 0; i < r.animations.Size(); ++i) {
        Animation& anim = r.animations[i];

        aiAnimation* ai_anim = new aiAnimation();
        ai_anim->mName = anim.name;
        ai_anim->mDuration = 0;
        ai_anim->mTicksPerSecond = 0;

        std::unordered_map<unsigned int, AnimationSamplers> samplers = GatherSamplers(anim);

        ai_anim->mNumChannels = samplers.size();
        if (ai_anim->mNumChannels > 0) {
            ai_anim->mChannels = new aiNodeAnim*[ai_anim->mNumChannels];
            int j = 0;
            for (auto& iter : samplers) {
                ai_anim->mChannels[j] = CreateNodeAnim(r, r.nodes[iter.first], iter.second);
                ++j;
            }
        }

        // Use the latest keyframe for the duration of the animation
        double maxDuration = 0;
        for (unsigned int j = 0; j < ai_anim->mNumChannels; ++j) {
            auto chan = ai_anim->mChannels[j];
            if (chan->mNumPositionKeys) {
                auto lastPosKey = chan->mPositionKeys[chan->mNumPositionKeys - 1];
                if (lastPosKey.mTime > maxDuration) {
                    maxDuration = lastPosKey.mTime;
                }
            }
            if (chan->mNumRotationKeys) {
                auto lastRotKey = chan->mRotationKeys[chan->mNumRotationKeys - 1];
                if (lastRotKey.mTime > maxDuration) {
                    maxDuration = lastRotKey.mTime;
                }
            }
            if (chan->mNumScalingKeys) {
                auto lastScaleKey = chan->mScalingKeys[chan->mNumScalingKeys - 1];
                if (lastScaleKey.mTime > maxDuration) {
                    maxDuration = lastScaleKey.mTime;
                }
            }
        }
        ai_anim->mDuration = maxDuration;

        mScene->mAnimations[i] = ai_anim;
    }
}
#endif

void glTF2Importer::ImportEmbeddedTextures(glTF2::Asset& r)
{
    embeddedTexIdxs.resize(r.images.Size(), -1);

    int numEmbeddedTexs = 0;
    for (size_t i = 0; i < r.images.Size(); ++i) {
        if (r.images[i].HasData())
            numEmbeddedTexs += 1;
    }

    if (numEmbeddedTexs == 0)
        return;

    mScene->mTextures = new aiTexture*[numEmbeddedTexs];

    // Add the embedded textures
    for (size_t i = 0; i < r.images.Size(); ++i) {
        Image &img = r.images[i];
        if (!img.HasData()) continue;

        int idx = mScene->mNumTextures++;
        embeddedTexIdxs[i] = idx;

        aiTexture* tex = mScene->mTextures[idx] = new aiTexture();

        size_t length = img.GetDataLength();
        void* data = img.StealData();

        tex->mWidth = static_cast<unsigned int>(length);
        tex->mHeight = 0;
        tex->pcData = reinterpret_cast<aiTexel*>(data);

        if (!img.mimeType.empty()) {
            const char* ext = strchr(img.mimeType.c_str(), '/') + 1;
            if (ext) {
                if (strcmp(ext, "jpeg") == 0) ext = "jpg";

                size_t len = strlen(ext);
                if (len <= 3) {
                    strcpy(tex->achFormatHint, ext);
                }
            }
        }
    }
}


void glTF2Importer::InternReadFile(const std::string& pFile, aiScene* pScene, IOSystem* pIOHandler) {

    this->mScene = pScene;

    // read the asset file
    glTF2::Asset asset(pIOHandler);
    asset.Load(pFile, GetExtension(pFile) == "glb");

    //
    // Copy the data out
    //

    ImportEmbeddedTextures(asset);
    ImportMaterials(asset);

    ImportMeshes(asset);

    ImportCameras(asset);

    ImportNodes(asset);

    ImportAnimations(asset);


    // TODO: it does not split the loaded vertices, should it?
    //pScene->mFlags |= AI_SCENE_FLAGS_NON_VERBOSE_FORMAT;

    //GVRf: not splitting vertices after import
    // MakeVerboseFormatProcess process;
    // process.Execute(pScene);
    if (pScene->mNumMeshes == 0) {
        pScene->mFlags |= AI_SCENE_FLAGS_INCOMPLETE;
    }
}

#endif // ASSIMP_BUILD_NO_GLTF_IMPORTER

