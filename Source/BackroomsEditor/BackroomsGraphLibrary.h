#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BackroomsGraphLibrary.generated.h"

/** Editor-only helpers callable from Python to rewire Blueprint graphs. */
UCLASS()
class BACKROOMSEDITOR_API UBackroomsGraphLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/** Lists every call node named FunctionName in all graphs of the Blueprint ("graph|node|function"). */
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Editor")
	static TArray<FString> ListCalls(UBlueprint* Blueprint, FName FunctionName);

	UFUNCTION(BlueprintCallable, Category = "Backrooms|Editor")
	static TArray<FString> GetBoneNames(USkeletalMesh* Mesh);

	/** Spawns one actor with a hierarchical ISM component holding all Transforms (world space). */
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Editor")
	static AActor* CreateInstancedActor(UStaticMesh* Mesh, const TArray<FTransform>& Transforms, FName Label, bool bCollision, float EndCullDistance);

	/** Bakes the current pose of a skeletal mesh actor into a StaticMesh asset (actor-local space, original materials). */
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Editor")
	static UStaticMesh* BakeSkeletalActorToStatic(AActor* SkeletalActor, const FString& PackageName);

	/** Before every TargetFunction call in event graphs: inserts a call to NewClass::NewFunction followed by a Delay. Returns count. */
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Editor")
	static int32 InsertCallAndDelayBefore(UBlueprint* Blueprint, FName TargetFunction, UClass* NewClass, FName NewFunction, float DelaySeconds);

	/** Retargets every call of OldFunction to NewClass::NewFunction (same pin names), recompiles, returns count. */
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Editor")
	static int32 ReplaceCall(UBlueprint* Blueprint, FName OldFunction, UClass* NewClass, FName NewFunction);
};
