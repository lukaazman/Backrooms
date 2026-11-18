#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "BackroomsDeath.generated.h"

class ACameraActor;
class APointLight;

/** Drop-in replacement for GameplayStatics::OpenLevel (same pins) that plays a face jump scare first. */
UCLASS()
class BACKROOMS_API UBackroomsDeath : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Death", meta = (WorldContext = "WorldContextObject", AdvancedDisplay = "bAbsolute,Options"))
	static void OpenLevelAfterJumpscare(const UObject* WorldContextObject, FName LevelName, bool bAbsolute = true, FString Options = TEXT(""));

	/** Plays the face jump scare only; the calling Blueprint continues (fade, widget) after its own delay. */
	UFUNCTION(BlueprintCallable, Category = "Backrooms|Death", meta = (WorldContext = "WorldContextObject"))
	static void PlayCaughtJumpscare(const UObject* WorldContextObject);
};

/** Runs the caught sequence: cut to the entity face, lunge, shake, light, fade, then open the level. */
UCLASS(NotBlueprintable)
class BACKROOMS_API ABackroomsDeathDirector : public AActor
{
	GENERATED_BODY()

public:
	ABackroomsDeathDirector();

	bool Begin(APlayerController* PC, APawn* Entity, FName InLevel, bool bInAbsolute, const FString& InOptions, bool bInOpenLevel = true);
	virtual void Tick(float DeltaSeconds) override;

private:
	static bool FindHead(APawn* Entity, FVector& OutAim, float& OutSize);

	TWeakObjectPtr<APlayerController> Player;
	TWeakObjectPtr<APawn> Entity;
	TWeakObjectPtr<ACameraActor> Cam;
	TWeakObjectPtr<APointLight> Light;
	FName Level;
	bool bAbsolute = true;
	FString Options;
	FVector FaceDir = FVector::ForwardVector;
	float MaxDist = 0.f;
	float HeadSize = 22.f;
	float Elapsed = 0.f;
	bool bOpened = false;
	bool bOpenLevel = true;

	float LightScale() const { return FMath::Square(FMath::Max(HeadSize, 5.f) / 22.f); }

	static constexpr float LungeTime = 0.25f;
	static constexpr float StartMul = 7.f;
	static constexpr float EndMul = 3.4f;
	static constexpr float FovStart = 70.f;
	static constexpr float FovEnd = 52.f;
	static constexpr float HoldEnd = 1.5f;
	static constexpr float LightBase = 4000.f;
	static constexpr float FadeTime = 0.5f;
};
