// VRM4U Copyright (c) 2021-2022 Haruyoshi Yamamoto. This software is released under the MIT License.

#include "VrmConvertMetadata.h"
#include "VrmConvert.h"


#include "VrmAssetListObject.h"
#include "VrmMetaObject.h"
#include "VrmLicenseObject.h"

#include "AssetRegistryModule.h"
#include "UObject/Package.h"
#include "UObject/UObjectGlobals.h"

#include "VrmJson.h"

#include <assimp/Importer.hpp>
#include <assimp/scene.h>
#include <assimp/mesh.h>
#include <assimp/postprocess.h>
#include <assimp/GltfMaterial.h>
#include <assimp/vrm/vrmmeta.h>


bool VRMConverter::Init(const uint8* pFileData, size_t dataSize, const aiScene *pScene) {
	jsonData.init(pFileData, dataSize);
	aiData = pScene;
	return true;
}


static UVrmLicenseObject *tmpLicense = nullptr;
UVrmLicenseObject* VRMConverter::GetVRMMeta(const aiScene *mScenePtr) {
	tmpLicense = nullptr;
	VRMConverter::ConvertVrmMeta(nullptr, mScenePtr, nullptr, 0);

	return tmpLicense;
}

bool VRMConverter::ConvertVrmFirst(UVrmAssetListObject* vrmAssetList, const uint8* pData, size_t dataSize) {

	// material
	if (VRMConverter::Options::Get().IsVRM10Model()) {
	} else {
		// alpha cutoff flag
		vrmAssetList->MaterialHasAlphaCutoff.Empty();
		for (auto& mat : jsonData.doc["materials"].GetArray()) {
			bool b = false;
			if (mat.HasMember("alphaCutoff")) {
				b = true;
			}
			vrmAssetList->MaterialHasAlphaCutoff.Add(b);
		}
	}

	return true;
}


bool VRMConverter::ConvertVrmMeta(UVrmAssetListObject *vrmAssetList, const aiScene *mScenePtr, const uint8* pData, size_t dataSize) {

	tmpLicense = nullptr;
	VRM::VRMMetadata *meta = reinterpret_cast<VRM::VRMMetadata*>(mScenePtr->mVRMMeta);

	UVrmMetaObject *m = nullptr;
	UVrmLicenseObject *lic = nullptr;

	{
		UPackage *package = GetTransientPackage();

		if (vrmAssetList) {
			package = vrmAssetList->Package;
		}

		if (package == GetTransientPackage() || vrmAssetList==nullptr) {
			m = VRM4U_NewObject<UVrmMetaObject>(package, NAME_None, EObjectFlags::RF_Public | RF_Transient, NULL);
			lic = VRM4U_NewObject<UVrmLicenseObject>(package, NAME_None, EObjectFlags::RF_Public | RF_Transient, NULL);
		} else {
			m = VRM4U_NewObject<UVrmMetaObject>(package, *(FString(TEXT("VM_")) + vrmAssetList->BaseFileName + TEXT("_VrmMeta")), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
			lic = VRM4U_NewObject<UVrmLicenseObject>(package, *(FString(TEXT("VL_")) + vrmAssetList->BaseFileName + TEXT("_VrmLicense")), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
		}

		if (vrmAssetList) {
			vrmAssetList->VrmMetaObject = m;
			vrmAssetList->VrmLicenseObject = lic;
			m->VrmAssetListObject = vrmAssetList;
		} else {
			tmpLicense = lic;
		}
	}

	if (meta == nullptr) {
		return false;
	}

	// bone
	if (VRMConverter::Options::Get().IsVRM10Model()){
		// VRM10
		if (pData && dataSize) {
			auto &humanBone = jsonData.doc["extensions"]["VRMC_vrm"]["humanoid"]["humanBones"];
			auto &origBone = jsonData.doc["nodes"];

			for (auto& g : humanBone.GetObject()) {
				int node = g.value["node"].GetInt();

				if (node >= 0 && node < (int)origBone.Size()) {
					m->humanoidBoneTable.Add(UTF8_TO_TCHAR(g.name.GetString())) = UTF8_TO_TCHAR(origBone[node]["name"].GetString());
				} else {
					m->humanoidBoneTable.Add(UTF8_TO_TCHAR(g.name.GetString())) = "";
				}
			}
		}
	} else {
		for (auto& a : meta->humanoidBone) {
			m->humanoidBoneTable.Add(UTF8_TO_TCHAR(a.humanBoneName.C_Str())) = UTF8_TO_TCHAR(a.nodeName.C_Str());
		}
	}

	//shape
	if (VRMConverter::Options::Get().IsVRM10Model()) {
		// VRM10
		auto &presets = jsonData.doc["extensions"]["VRMC_vrm"]["expressions"]["preset"];

		m->BlendShapeGroup.SetNum(presets.Size());
		int presetIndex = -1;
		for (auto& presetData : presets.GetObject()){
			++presetIndex;

			m->BlendShapeGroup[presetIndex].name = UTF8_TO_TCHAR(presetData.name.GetString()); // ex happy

			auto &bindData = presetData.value["morphTargetBinds"];

			m->BlendShapeGroup[presetIndex].BlendShape.SetNum(bindData.Size());

			m->BlendShapeGroup[presetIndex].isBinary = presetData.value["isBinary"].GetBool();
			m->BlendShapeGroup[presetIndex].overrideBlink = presetData.value["overrideBlink"].GetString();
			m->BlendShapeGroup[presetIndex].overrideLookAt = presetData.value["overrideLookAt"].GetString();
			m->BlendShapeGroup[presetIndex].overrideMouth = presetData.value["overrideMouth"].GetString();

			int bindIndex = -1;
			for (auto &bind : bindData.GetArray()) {
				++bindIndex;

				//m->BlendShapeGroup[presetIndex].BlendShape[bindIndex].morphTargetName = UTF8_TO_TCHAR(aiGroup.bind[b].blendShapeName.C_Str());
				auto &targetShape = m->BlendShapeGroup[presetIndex].BlendShape[bindIndex];
				//targetShape.morphTargetName = UTF8_TO_TCHAR(aiGroup.bind[b].blendShapeName.C_Str());
				//targetShape.meshName = UTF8_TO_TCHAR(aiGroup.bind[b].meshName.C_Str());
				//targetShape.nodeName = UTF8_TO_TCHAR(aiGroup.bind[b].nodeName.C_Str());
				//targetShape.weight = aiGroup.bind[b].weight;
				targetShape.shapeIndex = bind["index"].GetInt();

				{
					int tmpNodeID = bind["node"].GetInt(); // adjust offset
					int tmpMeshID = jsonData.doc["nodes"].GetArray()[tmpNodeID]["mesh"].GetInt();

					//meshID offset
					int offset = 0;
					for (int meshNo = 0; meshNo < tmpMeshID; ++meshNo) {
						if (jsonData.doc["meshes"].GetArray()[meshNo].HasMember("primitives") == false) continue;
						offset += jsonData.doc["meshes"].GetArray()[meshNo]["primitives"].Size() - 1;
					}
					targetShape.meshID = tmpMeshID + offset;
				}

				if (targetShape.meshID < (int)jsonData.doc["meshes"].Size()) {
					{
						auto& targetNames = jsonData.doc["meshes"].GetArray()[targetShape.meshID]["extras"]["targetNames"];
						if (targetShape.shapeIndex < (int)targetNames.Size()) {
							targetShape.morphTargetName = UTF8_TO_TCHAR(targetNames.GetArray()[targetShape.shapeIndex].GetString());
						}
					}

					{
						auto& tmp = jsonData.doc["meshes"][targetShape.meshID]["primitives"]["extras"]["targetNames"];
						if (targetShape.shapeIndex < (int)tmp.Size()) {
							targetShape.morphTargetName = tmp[targetShape.shapeIndex].GetString();

							if (VRMConverter::Options::Get().IsStrictMorphTargetNameMode()) {
								targetShape.morphTargetName = VRMConverter::NormalizeFileName(targetShape.morphTargetName);
							}
						}
					}
					targetShape.meshName = UTF8_TO_TCHAR(jsonData.doc["meshes"].GetArray()[targetShape.meshID]["name"].GetString());
				}
			}
		}
	} else {
		m->BlendShapeGroup.SetNum(meta->blensShapeGroupNum);
		for (int i = 0; i < meta->blensShapeGroupNum; ++i) {
			auto& aiGroup = meta->blensShapeGourp[i];

			m->BlendShapeGroup[i].name = UTF8_TO_TCHAR(aiGroup.groupName.C_Str());

			m->BlendShapeGroup[i].BlendShape.SetNum(aiGroup.bindNum);
			for (int b = 0; b < aiGroup.bindNum; ++b) {
				auto& bind = m->BlendShapeGroup[i].BlendShape[b];
				bind.morphTargetName = UTF8_TO_TCHAR(aiGroup.bind[b].blendShapeName.C_Str());
				bind.meshName = UTF8_TO_TCHAR(aiGroup.bind[b].meshName.C_Str());
				bind.nodeName = UTF8_TO_TCHAR(aiGroup.bind[b].nodeName.C_Str());
				bind.weight = aiGroup.bind[b].weight;
				bind.meshID = aiGroup.bind[b].meshID;
				bind.shapeIndex = aiGroup.bind[b].shapeIndex;

				if (VRMConverter::Options::Get().IsStrictMorphTargetNameMode()) {
					bind.morphTargetName = VRMConverter::NormalizeFileName(bind.morphTargetName);
				}
			}
		}
	}

	// tmp shape...
	if (pData && vrmAssetList) {
		if (VRMConverter::Options::Get().IsVRM10Model()) {
		} else {
			TMap<FString, FString> ParamTable;

			ParamTable.Add("_Color", "mtoon_Color");
			ParamTable.Add("_RimColor", "mtoon_RimColor");
			ParamTable.Add("_EmisionColor", "mtoon_EmissionColor");
			ParamTable.Add("_OutlineColor", "mtoon_OutColor");

			auto& group = jsonData.doc["extensions"]["VRM"]["blendShapeMaster"]["blendShapeGroups"];
			for (int i = 0; i < (int)group.Size(); ++i) {
				auto& bind = m->BlendShapeGroup[i];

				auto& shape = group[i];
				if (shape.HasMember("materialValues") == false) {
					continue;
				}
				for (auto& mat : shape["materialValues"].GetArray()) {
					FVrmBlendShapeMaterialList mlist;
					mlist.materialName = mat["materialName"].GetString();
					mlist.propertyName = mat["propertyName"].GetString();

					FString *tmp = vrmAssetList->MaterialNameOrigToAsset.Find(NormalizeFileName(mlist.materialName));
					if (tmp == nullptr) {
						continue;
					}
					mlist.materialName = *tmp;
					if (ParamTable.Find(mlist.propertyName)) {
						mlist.propertyName = ParamTable[mlist.propertyName];
					}
					mlist.color = FLinearColor(
						mat["targetValue"].GetArray()[0].GetFloat(),
						mat["targetValue"].GetArray()[1].GetFloat(),
						mat["targetValue"].GetArray()[2].GetFloat(),
						mat["targetValue"].GetArray()[3].GetFloat());
					bind.MaterialList.Add(mlist);
				}
			}
		}
	}

	if (VRMConverter::Options::Get().IsVRM10Model()) {
		// collider
		{
			{
				auto& collider = jsonData.doc["extensions"]["VRMC_springBone"]["colliders"];

				m->VRMColliderMeta.SetNum(collider.Size());
				for (int colliderNo = 0; colliderNo < (int)collider.Size(); ++colliderNo) {
					auto& dstCollider = m->VRMColliderMeta[colliderNo];
					dstCollider.bone = collider.GetArray()[colliderNo]["node"].GetInt();
					//dstCollider.boneName

					if (collider.GetArray()[colliderNo]["shape"].HasMember("sphere")) {
						dstCollider.collider.SetNum(1);
						dstCollider.collider[0].offset.Set(
							collider.GetArray()[colliderNo]["shape"]["sphere"]["offset"][0].GetFloat(),
							collider.GetArray()[colliderNo]["shape"]["sphere"]["offset"][1].GetFloat(),
							collider.GetArray()[colliderNo]["shape"]["sphere"]["offset"][2].GetFloat());
						dstCollider.collider[0].radius = collider.GetArray()[colliderNo]["shape"]["sphere"]["radius"].GetFloat();
						dstCollider.collider[0].shapeType = TEXT("sphere");

					}

					if (collider.GetArray()[colliderNo]["shape"].HasMember("capsule")) {
						dstCollider.collider.SetNum(1);
						dstCollider.collider[0].offset.Set(
							collider.GetArray()[colliderNo]["shape"]["capsule"]["offset"][0].GetFloat(),
							collider.GetArray()[colliderNo]["shape"]["capsule"]["offset"][1].GetFloat(),
							collider.GetArray()[colliderNo]["shape"]["capsule"]["offset"][2].GetFloat());
						dstCollider.collider[0].radius = collider.GetArray()[colliderNo]["shape"]["capsule"]["radius"].GetFloat();
						dstCollider.collider[0].tail.Set(
							collider.GetArray()[colliderNo]["shape"]["capsule"]["tail"][0].GetFloat(),
							collider.GetArray()[colliderNo]["shape"]["capsule"]["tail"][1].GetFloat(),
							collider.GetArray()[colliderNo]["shape"]["capsule"]["tail"][2].GetFloat());
						dstCollider.collider[0].shapeType = TEXT("capsule");
					}
				}
			}
			{
				auto& colliderGroup = jsonData.doc["extensions"]["VRMC_springBone"]["colliderGroups"]["colliderGroups"];
				auto& dstCollider = m->VRMColliderGroupMeta;

				dstCollider.SetNum(colliderGroup.Size());
				for (int i = 0; i < dstCollider.Num(); ++i) {
					auto c = colliderGroup.GetArray();
					dstCollider[i].groupName = c[i]["name"].GetString();

					for (uint32_t g = 0; g < c[i]["colliders"].Size(); ++g) {
						dstCollider[i].colliderGroup.Add(c[i]["colliders"].GetArray()[g].GetInt());
					}
				
				}


			}
		}

		//spring
		/*
		{
			auto& spring = jsonData.doc["extensions"]["VRMC_springBone"]["springs"];

			m->VRMSpringMeta.SetNum(spring.Size());
			for (int springNo = 0; springNo < spring.Size(); ++springNo) {
				auto& joint = spring.GetArray()[springNo]["joints"];

				auto& dstSpring = m->VRMSpringMeta[springNo];
				for (int jointNo = 0; jointNo < joint.Size(); ++jointNo) {
					auto& j = joint.GetArray()[jointNo];

					s.stiffness = vrms.stiffness;
					s.gravityPower = vrms.gravityPower;
					s.gravityDir.Set(vrms.gravityDir[0], vrms.gravityDir[1], vrms.gravityDir[2]);
					s.dragForce = vrms.dragForce;
					s.hitRadius = vrms.hitRadius;

					s.bones.SetNum(vrms.boneNum);
					s.boneNames.SetNum(vrms.boneNum);
					for (int b = 0; b < vrms.boneNum; ++b) {
						s.bones[b] = vrms.bones[b];
						s.boneNames[b] = UTF8_TO_TCHAR(vrms.bones_name[b].C_Str());
					}


					s.ColliderIndexArray.SetNum(vrms.colliderGourpNum);
					for (int c = 0; c < vrms.colliderGourpNum; ++c) {
						s.ColliderIndexArray[c] = vrms.colliderGroups[c];
					}

				}
			}
		}
		*/

	} else {
		// spring
		m->VRMSpringMeta.SetNum(meta->springNum);
		for (int i = 0; i < meta->springNum; ++i) {
			const auto& vrms = meta->springs[i];

			auto& s = m->VRMSpringMeta[i];
			s.stiffness = vrms.stiffness;
			s.gravityPower = vrms.gravityPower;
			s.gravityDir.Set(vrms.gravityDir[0], vrms.gravityDir[1], vrms.gravityDir[2]);
			s.dragForce = vrms.dragForce;
			s.hitRadius = vrms.hitRadius;

			s.bones.SetNum(vrms.boneNum);
			s.boneNames.SetNum(vrms.boneNum);
			for (int b = 0; b < vrms.boneNum; ++b) {
				s.bones[b] = vrms.bones[b];
				s.boneNames[b] = UTF8_TO_TCHAR(vrms.bones_name[b].C_Str());
			}


			s.ColliderIndexArray.SetNum(vrms.colliderGourpNum);
			for (int c = 0; c < vrms.colliderGourpNum; ++c) {
				s.ColliderIndexArray[c] = vrms.colliderGroups[c];
			}
		}

		//collider
		m->VRMColliderMeta.SetNum(meta->colliderGroupNum);
		for (int i = 0; i < meta->colliderGroupNum; ++i) {
			const auto& vrmc = meta->colliderGroups[i];

			auto& c = m->VRMColliderMeta[i];
			c.bone = vrmc.node;
			c.boneName = UTF8_TO_TCHAR(vrmc.node_name.C_Str());

			c.collider.SetNum(vrmc.colliderNum);
			for (int b = 0; b < vrmc.colliderNum; ++b) {
				c.collider[b].offset = FVector(vrmc.colliders[b].offset[0], vrmc.colliders[b].offset[1], vrmc.colliders[b].offset[2]);
				c.collider[b].radius = vrmc.colliders[b].radius;
			}
		}
	}

	// license
	{
		struct TT {
			FString key;
			FString &dst;
		};
		const TT table[] = {
			{TEXT("version"),		lic->version},
			{TEXT("author"),			lic->author},
			{TEXT("contactInformation"),	lic->contactInformation},
			{TEXT("reference"),		lic->reference},
				// texture skip
			{TEXT("title"),			lic->title},
			{TEXT("allowedUserName"),	lic->allowedUserName},
			{TEXT("violentUsageName"),	lic->violentUsageName},
			{TEXT("sexualUsageName"),	lic->sexualUsageName},
			{TEXT("commercialUsageName"),	lic->commercialUsageName},
			{TEXT("otherPermissionUrl"),		lic->otherPermissionUrl},
			{TEXT("licenseName"),			lic->licenseName},
			{TEXT("otherLicenseUrl"),		lic->otherLicenseUrl},

			{TEXT("violentUssageName"),	lic->violentUsageName},
			{TEXT("sexualUssageName"),	lic->sexualUsageName},
			{TEXT("commercialUssageName"),	lic->commercialUsageName},
		};
		for (int i = 0; i < meta->license.licensePairNum; ++i) {

			auto &p = meta->license.licensePair[i];

			for (auto &t : table) {
				if (t.key == p.Key.C_Str()) {
					t.dst = UTF8_TO_TCHAR(p.Value.C_Str());
				}
			}
			if (vrmAssetList) {
				if (FString(TEXT("texture")) == p.Key.C_Str()) {
					int t = FCString::Atoi(*FString(p.Value.C_Str()));
					if (t >= 0 && t < vrmAssetList->Textures.Num()) {
						lic->thumbnail = vrmAssetList->Textures[t];
					}
				}
			}
		}
	}

	return true;
}

bool VRMConverter::ConvertVrmMetaRenamed(UVrmAssetListObject* vrmAssetList, const aiScene* mScenePtr, const uint8* pData, size_t dataSize) {
	if (VRMConverter::Options::Get().IsGenerateHumanoidRenamedMesh()) {
		UPackage* package = GetTransientPackage();

		if (vrmAssetList) {
			package = vrmAssetList->Package;
		}

		UVrmMetaObject* m = vrmAssetList->VrmMetaObject;

		//m2 = VRM4U_NewObject<UVrmMetaObject>(package, *(FString(TEXT("VM_")) + vrmAssetList->BaseFileName + TEXT("_VrmMeta")), EObjectFlags::RF_Public | EObjectFlags::RF_Standalone);
		{
			TMap<FString, FString> BoneTable;

			UVrmMetaObject* m2 = DuplicateObject<UVrmMetaObject>(m, package, *(FString(TEXT("VM_")) + vrmAssetList->BaseFileName + TEXT("_ue4mannequin_VrmMeta")));
			vrmAssetList->VrmMannequinMetaObject = m2;
			for (auto& a : m2->humanoidBoneTable) {
				auto c = VRMUtil::table_ue4_vrm.FindByPredicate([a](const VRMUtil::VRMBoneTable& data) {
					// meta humanoid key == utiltable vrmbone
					return a.Key.ToLower() == data.BoneVRM.ToLower();
				});
				if (c) {
					if (a.Value != "") {
						BoneTable.Add(a.Value, c->BoneUE4);
						a.Value = c->BoneUE4;
					}
				}
			}
			for (auto& a : m2->VRMColliderMeta) {
				auto c = BoneTable.Find(a.boneName);
				if (c) {
					if (a.boneName != "") {
						a.boneName = *c;
					}
				}
			}
			for (auto& a : m2->VRMSpringMeta) {
				for (auto& b : a.boneNames) {
					auto c = BoneTable.Find(b);
					//auto c = VRMUtil::table_ue4_vrm.FindByPredicate([b](const VRMUtil::VRMBoneTable& data) { return b.ToLower() == data.BoneVRM.ToLower(); });
					if (c) {
						if (b != "") {
							b = *c;
						}
					}
				}
			}
		}

		{
			TMap<FString, FString> BoneTable;

			UVrmMetaObject* m3 = DuplicateObject<UVrmMetaObject>(m, package, *(FString(TEXT("VM_")) + vrmAssetList->BaseFileName + TEXT("_humanoid_VrmMeta")));
			vrmAssetList->VrmHumanoidMetaObject = m3;
			for (auto& a : m3->humanoidBoneTable) {
				if (a.Value != "") {
					BoneTable.Add(a.Value, a.Key);
					a.Value = a.Key;
				}
			}
			for (auto& a : m3->VRMColliderMeta) {
				auto c = BoneTable.Find(a.boneName);
				if (c) {
					if (a.boneName != "") {
						a.boneName = *c;
					}
				}
			}
			for (auto& a : m3->VRMSpringMeta) {
				for (auto& b : a.boneNames) {
					auto c = BoneTable.Find(b);
					//auto c = VRMUtil::table_ue4_vrm.FindByPredicate([b](const VRMUtil::VRMBoneTable& data) { return b.ToLower() == data.BoneVRM.ToLower(); });
					if (c) {
						if (b != "") {
							b = *c;
						}
					}
				}
			}
		}
	}
	return true;
}


VrmConvertMetadata::VrmConvertMetadata()
{
}

VrmConvertMetadata::~VrmConvertMetadata()
{
}
