#include "BackroomsGraphLibrary.h"

#include "EdGraph/EdGraph.h"
#include "Engine/Blueprint.h"
#include "Components/HierarchicalInstancedStaticMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Editor.h"
#include "AssetRegistry/AssetRegistryModule.h"
#include "Engine/SkinnedAssetCommon.h"
#include "MeshDescription.h"
#include "StaticMeshAttributes.h"
#include "StaticMeshOperations.h"
#include "Misc/PackageName.h"
#include "UObject/Package.h"
#include "Engine/MeshMerging.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EdGraphSchema_K2.h"
#include "K2Node_CallFunction.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"

TArray<FString> UBackroomsGraphLibrary::ListCalls(UBlueprint* Blueprint, FName FunctionName)
{
	TArray<FString> Result;
	if (!Blueprint)
	{
		return Result;
	}
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* G : Graphs)
	{
		for (UEdGraphNode* N : G->Nodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(N);
			if (Call && Call->FunctionReference.GetMemberName() == FunctionName)
			{
				Result.Add(FString::Printf(TEXT("%s|%s|%s"), *G->GetName(), *Call->GetName(), *FunctionName.ToString()));
			}
		}
	}
	return Result;
}

TArray<FString> UBackroomsGraphLibrary::GetBoneNames(USkeletalMesh* Mesh)
{
	TArray<FString> Out;
	if (Mesh)
	{
		const FReferenceSkeleton& Ref = Mesh->GetRefSkeleton();
		for (int32 i = 0; i < Ref.GetNum(); ++i)
		{
			Out.Add(Ref.GetBoneName(i).ToString());
		}
	}
	return Out;
}

AActor* UBackroomsGraphLibrary::CreateInstancedActor(UStaticMesh* Mesh, const TArray<FTransform>& Transforms, FName Label, bool bCollision, float EndCullDistance)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || !Mesh)
	{
		return nullptr;
	}
	AActor* Actor = World->SpawnActor<AActor>();
	USceneComponent* Root = NewObject<USceneComponent>(Actor, TEXT("Root"));
	Actor->SetRootComponent(Root);
	Root->RegisterComponent();
	UHierarchicalInstancedStaticMeshComponent* C = NewObject<UHierarchicalInstancedStaticMeshComponent>(Actor, TEXT("Instances"));
	C->SetupAttachment(Root);
	C->SetStaticMesh(Mesh);
	C->SetMobility(EComponentMobility::Static);
	C->SetCollisionEnabled(bCollision ? ECollisionEnabled::QueryAndPhysics : ECollisionEnabled::NoCollision);
	if (EndCullDistance > 0.f)
	{
		C->SetCullDistances(0, (int32)EndCullDistance);
	}
	Actor->AddInstanceComponent(C);
	C->RegisterComponent();
	C->AddInstances(Transforms, false, true);
	C->BuildTreeIfOutdated(true, true);
	Actor->SetActorLabel(Label.ToString());
	return Actor;
}

UStaticMesh* UBackroomsGraphLibrary::BakeSkeletalActorToStatic(AActor* Actor, const FString& PackageName)
{
	USkeletalMeshComponent* Comp = Actor ? Actor->FindComponentByClass<USkeletalMeshComponent>() : nullptr;
	USkeletalMesh* Skel = Comp ? Comp->GetSkeletalMeshAsset() : nullptr;
	if (!Skel)
	{
		return nullptr;
	}
	// shortcut: bakes the bind pose (checked against the posed bounds by the caller), no per-bone skinning
	UPackage* Pkg = CreatePackage(*PackageName);
	UStaticMesh* SM = NewObject<UStaticMesh>(Pkg, FPackageName::GetShortFName(PackageName), RF_Public | RF_Standalone);
	FStaticMeshSourceModel& Src = SM->AddSourceModel();
	Src.BuildSettings.bRecomputeNormals = false;
	Src.BuildSettings.bRecomputeTangents = false;
	Src.BuildSettings.bGenerateLightmapUVs = false;
	FMeshDescription* Dst = SM->CreateMeshDescription(0);
	FMeshDescription Skin;
	Skel->GetMeshDescription(0, Skin);
	// copy geometry into a clean static description: skeletal attributes do not survive a static mesh save/load
	FStaticMeshAttributes DstA(*Dst);
	DstA.Register();
	FStaticMeshConstAttributes SrcA(Skin);
	TMap<FVertexID, FVertexID> VMap;
	TMap<FVertexInstanceID, FVertexInstanceID> VIMap;
	TMap<FPolygonGroupID, FPolygonGroupID> PGMap;
	for (FVertexID V : Skin.Vertices().GetElementIDs())
	{
		const FVertexID N = Dst->CreateVertex();
		DstA.GetVertexPositions()[N] = SrcA.GetVertexPositions()[V];
		VMap.Add(V, N);
	}
	for (FPolygonGroupID P : Skin.PolygonGroups().GetElementIDs())
	{
		const FPolygonGroupID N = Dst->CreatePolygonGroup();
		DstA.GetPolygonGroupMaterialSlotNames()[N] = SrcA.GetPolygonGroupMaterialSlotNames()[P];
		PGMap.Add(P, N);
	}
	for (FVertexInstanceID VI : Skin.VertexInstances().GetElementIDs())
	{
		const FVertexInstanceID N = Dst->CreateVertexInstance(VMap[Skin.GetVertexInstanceVertex(VI)]);
		DstA.GetVertexInstanceNormals()[N] = SrcA.GetVertexInstanceNormals()[VI];
		DstA.GetVertexInstanceTangents()[N] = SrcA.GetVertexInstanceTangents()[VI];
		DstA.GetVertexInstanceBinormalSigns()[N] = SrcA.GetVertexInstanceBinormalSigns()[VI];
		DstA.GetVertexInstanceColors()[N] = SrcA.GetVertexInstanceColors()[VI];
		DstA.GetVertexInstanceUVs().Set(N, 0, SrcA.GetVertexInstanceUVs().Get(VI, 0));
		VIMap.Add(VI, N);
	}
	for (FTriangleID T : Skin.Triangles().GetElementIDs())
	{
		TArrayView<const FVertexInstanceID> Vis = Skin.GetTriangleVertexInstances(T);
		Dst->CreateTriangle(PGMap[Skin.GetTrianglePolygonGroup(T)], { VIMap[Vis[0]], VIMap[Vis[1]], VIMap[Vis[2]] });
	}
	// pipes are single-bone: move the bind-pose verts by (posed bone * inverse bind bone)
	Comp->InitAnim(true);
	Comp->TickAnimation(0.f, false);
	Comp->RefreshBoneTransforms();
	const TArray<FTransform>& Posed = Comp->GetComponentSpaceTransforms();
	const FReferenceSkeleton& RefSk = Skel->GetRefSkeleton();
	if (RefSk.GetNum() == 1 && Posed.Num() == 1)
	{
		FStaticMeshOperations::ApplyTransform(*Dst, Posed[0] * RefSk.GetRefBonePose()[0].Inverse());
	}
	SM->CommitMeshDescription(0);
	for (const FSkeletalMaterial& M : Skel->GetMaterials())
	{
		SM->GetStaticMaterials().Add(FStaticMaterial(M.MaterialInterface, M.MaterialSlotName));
	}
	SM->Build(true);
	FAssetRegistryModule::AssetCreated(SM);
	Pkg->MarkPackageDirty();
	return SM;
}

static UK2Node_CallFunction* MakeCall(UEdGraph* Graph, UFunction* Func, const FVector2D& Pos)
{
	UK2Node_CallFunction* N = NewObject<UK2Node_CallFunction>(Graph);
	N->SetFromFunction(Func);
	N->NodePosX = (int32)Pos.X;
	N->NodePosY = (int32)Pos.Y;
	Graph->AddNode(N, false, false);
	N->CreateNewGuid();
	N->PostPlacedNewNode();
	N->AllocateDefaultPins();
	return N;
}

int32 UBackroomsGraphLibrary::InsertCallAndDelayBefore(UBlueprint* Blueprint, FName TargetFunction, UClass* NewClass, FName NewFunction, float DelaySeconds)
{
	UFunction* Func = (Blueprint && NewClass) ? NewClass->FindFunctionByName(NewFunction) : nullptr;
	UFunction* DelayFunc = UKismetSystemLibrary::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UKismetSystemLibrary, Delay));
	if (!Func || !DelayFunc)
	{
		return 0;
	}
	const UEdGraphSchema_K2* K2 = GetDefault<UEdGraphSchema_K2>();
	int32 Count = 0;
	for (UEdGraph* G : Blueprint->UbergraphPages)
	{
		TArray<UK2Node_CallFunction*> Targets;
		for (UEdGraphNode* N : G->Nodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(N);
			if (Call && Call->FunctionReference.GetMemberName() == TargetFunction)
			{
				Targets.Add(Call);
			}
		}
		for (UK2Node_CallFunction* T : Targets)
		{
			UEdGraphPin* In = T->GetExecPin();
			if (!In || In->LinkedTo.Num() == 0)
			{
				continue;
			}
			const FVector2D P(T->NodePosX - 600, T->NodePosY);
			UK2Node_CallFunction* A = MakeCall(G, Func, P);
			UK2Node_CallFunction* D = MakeCall(G, DelayFunc, P + FVector2D(300, 0));
			D->FindPinChecked(TEXT("Duration"))->DefaultValue = FString::SanitizeFloat(DelaySeconds);
			TArray<UEdGraphPin*> Prev = In->LinkedTo;
			In->BreakAllPinLinks();
			for (UEdGraphPin* L : Prev)
			{
				K2->TryCreateConnection(L, A->GetExecPin());
			}
			K2->TryCreateConnection(A->GetThenPin(), D->GetExecPin());
			K2->TryCreateConnection(D->GetThenPin(), In);
			++Count;
		}
	}
	if (Count > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}
	return Count;
}

int32 UBackroomsGraphLibrary::ReplaceCall(UBlueprint* Blueprint, FName OldFunction, UClass* NewClass, FName NewFunction)
{
	UFunction* Func = (Blueprint && NewClass) ? NewClass->FindFunctionByName(NewFunction) : nullptr;
	if (!Func)
	{
		return 0;
	}
	int32 Count = 0;
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* G : Graphs)
	{
		for (UEdGraphNode* N : G->Nodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(N);
			if (Call && Call->FunctionReference.GetMemberName() == OldFunction)
			{
				Call->SetFromFunction(Func);
				Call->ReconstructNode();
				++Count;
			}
		}
	}
	if (Count > 0)
	{
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FKismetEditorUtilities::CompileBlueprint(Blueprint);
	}
	return Count;
}
