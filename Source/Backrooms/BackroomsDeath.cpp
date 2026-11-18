#include "BackroomsDeath.h"

#include "Camera/CameraActor.h"
#include "Camera/CameraComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/PlayerController.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"

static bool StartDirector(const UObject* WorldContextObject, FName LevelName, bool bAbsolute, const FString& Options, bool bOpenLevel)
{
	UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
	if (!World)
	{
		return false;
	}
	for (TActorIterator<ABackroomsDeathDirector> It(World); It; ++It)
	{
		return true; // sequence already running
	}
	APlayerController* PC = UGameplayStatics::GetPlayerController(World, 0);
	APawn* PlayerPawn = PC ? PC->GetPawn() : nullptr;
	APawn* Best = nullptr;
	float BestDist = 1500.f;
	if (PlayerPawn)
	{
		for (TActorIterator<APawn> It(World); It; ++It)
		{
			APawn* P = *It;
			if (P == PlayerPawn || !P->FindComponentByClass<USkeletalMeshComponent>() || P->IsHidden())
			{
				continue;
			}
			const float D = FVector::Dist(P->GetActorLocation(), PlayerPawn->GetActorLocation());
			if (D < BestDist)
			{
				BestDist = D;
				Best = P;
			}
		}
	}
	ABackroomsDeathDirector* Dir = Best ? World->SpawnActor<ABackroomsDeathDirector>() : nullptr;
	return Dir && Dir->Begin(PC, Best, LevelName, bAbsolute, Options, bOpenLevel);
}

void UBackroomsDeath::OpenLevelAfterJumpscare(const UObject* WorldContextObject, FName LevelName, bool bAbsolute, FString Options)
{
	if (!StartDirector(WorldContextObject, LevelName, bAbsolute, Options, true))
	{
		UGameplayStatics::OpenLevel(WorldContextObject, LevelName, bAbsolute, Options);
	}
}

void UBackroomsDeath::PlayCaughtJumpscare(const UObject* WorldContextObject)
{
	StartDirector(WorldContextObject, NAME_None, true, FString(), false);
}

ABackroomsDeathDirector::ABackroomsDeathDirector()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bTickEvenWhenPaused = true;
	SetActorTickEnabled(false);
	RootComponent = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
}

bool ABackroomsDeathDirector::FindHead(APawn* E, FVector& OutAim, float& OutSize)
{
	USkeletalMeshComponent* Mesh = E->FindComponentByClass<USkeletalMeshComponent>();
	if (!Mesh)
	{
		return false;
	}
	// face centre = halfway between the Head bone and the top of the head; fall back to the bounds
	const FName Head = Mesh->GetBoneIndex(TEXT("Head")) != INDEX_NONE ? FName(TEXT("Head")) : FName(TEXT("mixamorig:Head"));
	if (Mesh->GetBoneIndex(Head) != INDEX_NONE)
	{
		const FName Top(TEXT("HeadTop_End"));
		const bool bTop = Mesh->GetBoneIndex(Top) != INDEX_NONE;
		OutAim = bTop ? (Mesh->GetBoneLocation(Head) + Mesh->GetBoneLocation(Top)) * 0.5f : Mesh->GetBoneLocation(Head) + FVector(0, 0, 8.f);
		OutSize = FMath::Max3<float>(bTop ? (float)FVector::Dist(Mesh->GetBoneLocation(Head), Mesh->GetBoneLocation(Top)) : 22.f, (float)(Mesh->Bounds.BoxExtent.Z * 0.2f), 5.f); // oversized creatures: scale by body height too
		return true;
	}
	const FBoxSphereBounds B = Mesh->Bounds; // shortcut: no head bone, use top of bounds; upgrade with per-entity socket
	OutAim = B.Origin + FVector(0, 0, B.BoxExtent.Z * 0.8f);
	OutSize = B.BoxExtent.Z * 0.25f;
	return true;
}

bool ABackroomsDeathDirector::Begin(APlayerController* PC, APawn* InEntity, FName InLevel, bool bInAbsolute, const FString& InOptions, bool bInOpenLevel)
{
	FVector Aim;
	float Size = 22.f;
	if (!PC || !PC->GetPawn() || !InEntity || !FindHead(InEntity, Aim, Size))
	{
		return false;
	}
	Player = PC;
	Entity = InEntity;
	Level = InLevel;
	bAbsolute = bInAbsolute;
	Options = InOptions;
	bOpenLevel = bInOpenLevel;
	if (PC->PlayerCameraManager)
	{
		PC->PlayerCameraManager->StopCameraFade(); // the caller starts its own fade right before this runs
	}

	APawn* PlayerPawn = PC->GetPawn();
	FVector ToPlayer = PlayerPawn->GetActorLocation() - Aim;
	ToPlayer.Z = 0.f;
	ToPlayer = ToPlayer.GetSafeNormal();
	FVector Fwd = InEntity->GetActorForwardVector();
	Fwd.Z = 0.f;
	// the entity runs at the player, so its heading is the face direction; blend in the player direction against odd rotations
	FaceDir = (Fwd.GetSafeNormal() + ToPlayer * 0.5f).GetSafeNormal();
	if (FaceDir.IsNearlyZero())
	{
		FaceDir = ToPlayer;
	}

	HeadSize = Size;
	const float StartDist = Size * StartMul;
	MaxDist = StartDist;
	FHitResult Hit;
	FCollisionQueryParams Q(SCENE_QUERY_STAT(BackroomsDeathCam), false, InEntity);
	Q.AddIgnoredActor(PlayerPawn);
	if (GetWorld()->LineTraceSingleByChannel(Hit, Aim, Aim + FaceDir * StartDist, ECC_Visibility, Q))
	{
		MaxDist = FMath::Max(Hit.Distance * 0.8f, Size * EndMul);
	}

	FActorSpawnParameters SP;
	SP.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* C = GetWorld()->SpawnActor<ACameraActor>(Aim + FaceDir * MaxDist, (-FaceDir).Rotation(), SP);
	if (!C)
	{
		return false;
	}
	C->GetCameraComponent()->SetFieldOfView(FovStart);
	C->GetCameraComponent()->PostProcessSettings.bOverride_MotionBlurAmount = true;
	C->GetCameraComponent()->PostProcessSettings.MotionBlurAmount = 0.f;
	C->GetCameraComponent()->PostProcessBlendWeight = 1.f;
	Cam = C;

	APointLight* L = GetWorld()->SpawnActor<APointLight>(Aim + FaceDir * MaxDist + FVector(0, 0, 20.f), FRotator::ZeroRotator, SP);
	if (L)
	{
		L->SetMobility(EComponentMobility::Movable);
		UPointLightComponent* LC = L->PointLightComponent;
		LC->SetIntensity(LightBase * LightScale());
		LC->SetAttenuationRadius(450.f * FMath::Sqrt(LightScale()));
		LC->SetLightColor(FLinearColor(1.f, 0.93f, 0.85f));
		LC->SetCastShadows(false);
		Light = L;
	}

	PlayerPawn->DisableInput(PC);
	PC->SetIgnoreMoveInput(true);
	PC->SetIgnoreLookInput(true);
	if (ACharacter* PCh = Cast<ACharacter>(PlayerPawn))
	{
		PCh->GetCharacterMovement()->DisableMovement();
	}
	if (AController* AI = InEntity->GetController())
	{
		AI->StopMovement();
	}
	if (ACharacter* ECh = Cast<ACharacter>(InEntity))
	{
		ECh->GetCharacterMovement()->DisableMovement();
	}
	InEntity->SetActorTickEnabled(false); // freeze Blueprint logic and rotation, the mesh keeps animating

	if (USoundBase* Sting = LoadObject<USoundBase>(nullptr, TEXT("/Game/Sounds/FX/ChaseJumpScare.ChaseJumpScare")))
	{
		UGameplayStatics::PlaySound2D(this, Sting);
	}
	PC->SetViewTargetWithBlend(C, 0.f);
	SetActorTickEnabled(true);
	return true;
}

void ABackroomsDeathDirector::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	Elapsed += DeltaSeconds;
	APlayerController* PC = Player.Get();
	APawn* E = Entity.Get();
	ACameraActor* C = Cam.Get();
	FVector Aim;
	float Size = HeadSize;
	if (!PC || !C || !E || !FindHead(E, Aim, Size))
	{
		SetActorTickEnabled(false);
		return;
	}
	// camera rushes at the face: eased dolly + FOV squeeze, then shake that decays but never stops until the fade
	const float A = FMath::Clamp(Elapsed / LungeTime, 0.f, 1.f);
	const float Ease = A * A;
	const float Dist = FMath::Lerp(MaxDist, FMath::Min(HeadSize * EndMul, MaxDist), Ease);
	C->GetCameraComponent()->SetFieldOfView(FMath::Lerp(FovStart, FovEnd, Ease));
	const float Amp = ((Elapsed < LungeTime) ? 1.f : 2.5f * FMath::Exp(-(Elapsed - LungeTime) * 1.8f) + 0.5f) * FMath::Clamp(HeadSize / 22.f, 0.5f, 3.f);
	const FVector Pos = Aim + FaceDir * Dist + FMath::VRand() * Amp;
	C->SetActorLocation(Pos);
	FRotator Look = (Aim - Pos).Rotation();
	Look += FRotator(FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f), FMath::FRandRange(-1.f, 1.f)) * Amp;
	C->SetActorRotation(Look);
	if (APointLight* L = Light.Get())
	{
		L->SetActorLocation(Pos + FVector(0, 0, 20.f));
		L->PointLightComponent->SetIntensity(LightBase * LightScale() * FMath::FRandRange(0.7f, 1.2f));
	}
	if (Elapsed < HoldEnd && PC->PlayerCameraManager)
	{
		// the entity Blueprint starts further fades while its own timers run; keep the face on screen until our fade
		PC->PlayerCameraManager->StopCameraFade();
	}
	if (!bOpenLevel)
	{
		if (Elapsed >= HoldEnd)
		{
			SetActorTickEnabled(false); // the caller fades and shows its own UI; the camera stays on the face
		}
		return;
	}
	if (!bOpened && Elapsed >= HoldEnd)
	{
		if (PC->PlayerCameraManager)
		{
			PC->PlayerCameraManager->StartCameraFade(0.f, 1.f, FadeTime, FLinearColor::Black, true, true);
		}
		bOpened = true;
	}
	if (bOpened && Elapsed >= HoldEnd + FadeTime)
	{
		SetActorTickEnabled(false);
		UGameplayStatics::OpenLevel(this, Level, bAbsolute, Options);
	}
}
