// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Fusion/Types.h"
#include "FusionOnlineSubsystemSettings.h"
#include "Fusion/Broadcaster.h"
#include "Fusion/Client.h"
#include "FusionActorComponent.generated.h"

enum class ObjectSpecialFlags : uint8
{
	None = 0,
	IsRootTransform = 1 << 1,
	IgnoreRootTransformProperties = 1 << 2,
};

inline ObjectSpecialFlags operator&(ObjectSpecialFlags a, ObjectSpecialFlags b)
{
	return static_cast<ObjectSpecialFlags>(static_cast<uint8>(a) & static_cast<uint8>(b));
}

inline ObjectSpecialFlags operator|(ObjectSpecialFlags a, ObjectSpecialFlags b)
{
	return static_cast<ObjectSpecialFlags>(static_cast<uint8>(a) | static_cast<uint8>(b));
}

inline ObjectSpecialFlags& operator|=(ObjectSpecialFlags& a, ObjectSpecialFlags b)
{
	a = a | b;
	return a;
}

UENUM(BlueprintType)
enum class EFusionObjectOwnerFlags : uint8
{
	// Used for most objects, allows players to take and release ownership with transaction semantics
	Transaction = static_cast<uint8>(FusionCore::ObjectOwnerModes::Transaction),

	// Same as transaction but if a player is currently the owner of the object when he leaves, the object is destroyed
	PlayerAttached = static_cast<uint8>(FusionCore::ObjectOwnerModes::PlayerAttached),

	// Allows dynamic overriding of ownership on every client, useful for things that quickly changes ownership such as physics objects, etc.
	Dynamic = static_cast<uint8>(FusionCore::ObjectOwnerModes::Dynamic),

	// Object is always owned by who's considered to be the master client currently, if a new master client is sellected it automatically switches to him
	MasterClient = static_cast<uint8>(FusionCore::ObjectOwnerModes::MasterClient),

	// Object lifetime and ownership must be handled explicitly.
	GameGlobal = static_cast<uint8>(FusionCore::ObjectOwnerModes::GameGlobal)
};

UENUM(BlueprintType, Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class EObjectSpecialFlags : uint8
{
	None = 0 UMETA(Hidden),
	IsRootTransform = 1 << 1,
	IgnoreRootTransformProperties = 1 << 2,
	SceneObject = 1 << 3,
	ExistsOnClient = 1 << 4,
};
ENUM_CLASS_FLAGS(EObjectSpecialFlags);

USTRUCT(BlueprintType)
struct FFusionComponentRef
{
	GENERATED_BODY()
	
	UPROPERTY(EditAnywhere, Category = "Fusion")
	FString ComponentName;

	void GetComponent(const AActor* Owner);
};


USTRUCT(BlueprintType)
struct FTypeData
{
	GENERATED_BODY()

	FusionCore::TypeRef TypeRef;
	
	UPROPERTY()
	TWeakObjectPtr<UObject> Object;

	EObjectSpecialFlags SpecialFlags{};
};

class UFusionActorComponent;

USTRUCT(BlueprintType)
struct FPackagedSettings
{
	GENERATED_BODY()

	FPackagedSettings()
	: bForecastPhysicsEnabled(false)
	, bSkipPreNetReceive(false)
	, bSkipPostNetReceive(false)
	{
		
	}
	
	bool bForecastPhysicsEnabled;
	bool bSkipPreNetReceive;
	bool bSkipPostNetReceive;

	UPROPERTY()
	TObjectPtr<UFusionActorComponent> ActorSettings{nullptr};
};

UENUM(BlueprintType)
enum class ELocalStateCopyMode : uint8
{
	Auto UMETA(DisplayName = "Auto"),

	// Only copy local state to the networked state the frame after CopyLocalStateNextFrame() is called
	Manual UMETA(DisplayName = "Manual"),
};

UENUM(BlueprintType)
enum class EFusionObjectDestroyMode : uint8
{
	Local = 0 UMETA(DisplayName = "Local"),
	Remote = 1 UMETA(DisplayName = "Remote"),
	MapChange = 2 UMETA(DisplayName = "Map Change"),
	Shutdown = 3 UMETA(DisplayName = "Shutdown"),
	RejectedNotOwner = 4 UMETA(DisplayName = "Rejected Not Owner"),
	ForceDestroy = 5 UMETA(DisplayName = "Force Destroy")
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FFusionObjectStatusChange);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FFusionOnObjectDestroyed, EFusionObjectDestroyMode, Mode);

UCLASS(Blueprintable, ClassGroup=(Fusion), meta=(BlueprintSpawnableComponent, DisplayName = "Fusion Actor Component"))
class PHOTONFUSION_API UFusionActorComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFusionActorComponent();

	UPROPERTY(EditAnywhere, Category="Fusion")
	bool bSkipPreNetReceive{false};

	UPROPERTY(EditAnywhere, Category="Fusion")
	bool bSkipPostNetReceive{false};
	
	UPROPERTY(EditAnywhere, Category="Fusion")
	bool bSkipAutoAttach{false};

	UPROPERTY(EditAnywhere, Category="Fusion")
	bool bAutomaticallySendUpdates{true};

	// Set how the local state is copied to the networked state
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion")
	ELocalStateCopyMode LocalStateCopyMode = ELocalStateCopyMode::Auto;

	// When in Manual mode, request this actor's local state be copied to the networked state in the next tick
	UFUNCTION(BlueprintCallable, Category = "Fusion")
	void CopyLocalStateNextFrame();

	bool ConsumePendingLocalStateCopy();

	UPROPERTY(EditAnywhere, Category = "Fusion")
	TArray<FFusionComponentRef> ComponentsToSkipPreAndPostNetReceive;
	
	UPROPERTY(EditAnywhere, Category="Fusion")
	EFusionObjectOwnerFlags Ownership{0};

	EObjectSpecialFlags FusionObjectFlags{EObjectSpecialFlags::None};
	
	UPROPERTY(EditAnywhere, Category = "Fusion")
	bool bForecastPhysicsEnabled{true};
	
	UPROPERTY(EditAnywhere, Category = "Fusion")
	double AutoDynamicOwnershipRange {0}; 
	
	// Start of getters for settings

	float GetLinearVelCorrectionMul() const;

	float GetAngularVelCorrectionMul() const;

	float GetImpactStartCorrectionTime() const;

	float GetImpactCorrectionTimeComplete() const;

	float GetPositionCorrectionLerp() const;

	float GetRotationCorrectionLerp() const;

	float GetSpring() const;

	float GetDamper() const;

	float GetMinLinearDetectedError() const;

	float GetMinAngularDetectedError() const;

	float GetMaxLinearError() const;

	float GetMaxAngularError() const;

	float GetLowCorrectionProgressThreshold() const;

	float GetHighErrorSimilarityThreshold() const;

	float GetMaxErrorTotalTime() const;

	float GetMaxRemoteSleepIgnoreTime() const;

	float GetMaxExtrapolationTime() const;

	float GetMaxSpawnExtrapolationTime() const;

	float GetDeltaRotationScale() const;

	EFusionGravityForecast GetGravityForecast() const;

	EFusionPhysicsCorrection GetErrorCorrectionType() const;

	// End of getters for settings


#if WITH_EDITOR
	virtual void PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent) override;
	virtual void PostLoad() override;
#endif // WITH_EDITOR

	virtual void PostInitProperties() override;

	UFUNCTION(BlueprintNativeEvent, Category="Fusion")
	bool ShouldAddComponentType(UActorComponent* Component);
	virtual bool ShouldAddComponentType_Implementation(UActorComponent* Component);
	
	UFUNCTION(BlueprintCallable, Category="Fusion")
	void AddActorSource();

	UFUNCTION(BlueprintCallable, Category="Fusion")
	void ToggleNetworkSend(bool bToggle);
	
	FusionCore::ObjectOwnerModes GetOwnerMode();

	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type Reason) override;
	virtual void TickComponent(float DeltaTime, enum ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	
	FusionCore::ObjectOwnerModes GetTypes(const class UFusionClient* Client,  TSet<UActorComponent*> Components, USceneComponent* RootComponent, TArray<FTypeData>& OutTypeData);
	
	void CheckPhysicsReplication(const class UFusionClient* Client, AActor* Actor, TArray<FTypeData>& OutTypeData);
	
	FPackagedSettings PackageSettings();
	
	void RemoveEvents();
	void SubscribeEvents(FusionCore::Client* Client, FusionCore::ObjectId Id);
	
	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FFusionObjectStatusChange OnObjectReady;

	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FFusionOnObjectDestroyed OnObjectDestroyed;

	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FFusionObjectStatusChange OnOwnerChanged;

	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FFusionObjectStatusChange OnOwnerWasGiven;

	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FFusionObjectStatusChange OnInterestEnter;
	
	UPROPERTY(BlueprintAssignable, Category="Fusion")
	FFusionObjectStatusChange OnInterestExit;

private:

	PhotonCommon::SubscriptionBag BridgeSubscriptions;

	bool bLocalStateCopyPending = true;
	
	// Start of settings that override those found in PhotonOnlineSubsystemSettings.h
	

	// LinearVelCorrectionMul

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideLinearVelCorrectionMul = false;

	/// A scale factor applied to the correction velocity if [Velocity] or [PositionRotation] is the [Error Correction Type].
	/// Higher values means the body will be corrected faster, but can increase overshoot
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideLinearVelCorrectionMul"))
	float LinearVelCorrectionMul = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideLinearVelCorrectionMul = FLT_MAX;


	// AngularVelCorrectionMul

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideAngularVelCorrectionMul = false;

	/// A scale factor used when extrapolating the angular velocity. Higher values will result in a bigger extrapolated velocity.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideAngularVelCorrectionMul"))
	float AngularVelCorrectionMul = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideAngularVelCorrectionMul = FLT_MAX;


	// ImpactStartCorrectionTime

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideImpactStartCorrectionTime = false;

	/// How long after an impact do we apply a correction. Between the impact and this time no local correction is applied.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideImpactStartCorrectionTime", Units = "Seconds"))
	float ImpactStartCorrectionTime = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideImpactStartCorrectionTime = FLT_MAX;


	// ImpactCorrectionTimeComplete

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideImpactCorrectionTimeComplete = false;

	/// After 'ImpactStartCorrectionTime' we lerp from no correction to normal correction over this time.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideImpactCorrectionTimeComplete", Units = "Seconds"))
	float ImpactCorrectionTimeComplete = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideImpactCorrectionTimeComplete = FLT_MAX;


	// PositionCorrectionLerp

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverridePositionCorrectionLerp = false;

	/// Lerp object position from local to extrapolated (Range 0..1). Only used if the Error Correction Type is [Position Rotation].
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverridePositionCorrectionLerp", ClampMin = "0.0", ClampMax = "1.0"))
	float PositionCorrectionLerp = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverridePositionCorrectionLerp = FLT_MAX;


	// RotationCorrectionLerp

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideRotationCorrectionLerp = false;

	/// Lerp object rotation from local to extrapolated (Range 0..1)
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideRotationCorrectionLerp", ClampMin = "0.0", ClampMax = "1.0"))
	float RotationCorrectionLerp = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideRotationCorrectionLerp = FLT_MAX;


	// Spring

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideSpring = false;

	/// The intensity of the error correction, increase this value to make the error be corrected faster. If overshooting try increasing "Damper".
	/// Only used if the Error Correction Type is [Spring Damping].
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideSpring"))
	float Spring = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideSpring = FLT_MAX;


	// Damper

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideDamper = false;

	/// The resistance of the correction oscillation, increase this value to increase the resistance of the spring correction and control overshooting.
	/// Keep it bellow "Spring" value.
	/// Only used if the Error Correction Type is [Spring Damping].
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideDamper"))
	float Damper = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideDamper = FLT_MAX;


	// MinLinearDetectedError

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMinLinearDetectedError = false;

	/// Error must be over this value (metres) for correction to be applied. Errors below this threshold are not considered
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMinLinearDetectedError", Units = "Centimeters"))
	float MinLinearDetectedError = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMinLinearDetectedError = FLT_MAX;


	// MinAngularDetectedError

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMinAngularDetectedError = false;

	/// Error must be over this value(degrees) for correction to be applied. Errors below this threshold are not considered
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMinAngularDetectedError", Units = "Degrees"))
	float MinAngularDetectedError = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMinAngularDetectedError = FLT_MAX;


	// MaxLinearError

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMaxLinearError = false;

	/// Linear error above this threshold will force an immediate move to the remote physics state. Zero means no limit.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMaxLinearError", Units = "Centimeters"))
	float MaxLinearError = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMaxLinearError = FLT_MAX;


	// MaxAngularError

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMaxAngularError = false;

	/// Angular error above this threshold will force a immediate move to the remote physics state. Zero means no limit.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMaxAngularError", Units = "Degrees"))
	float MaxAngularError = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMaxAngularError = FLT_MAX;


	// LowCorrectionProgressThreshold

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideLowCorrectionProgressThreshold = false;

	/// If correction progress is below this accrue time towards a reset, in cm.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideLowCorrectionProgressThreshold"))
	float LowCorrectionProgressThreshold = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideLowCorrectionProgressThreshold = FLT_MAX;


	// HighErrorSimilarityThreshold

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideHighErrorSimilarityThreshold = false;

	/// If current error direction and magnitude dot previous error direction and magnitude is above this accrue time 
	/// towards a reset. This value roughly represents how little the direction of the linear error has changed, and how 
	/// big the error is. Roughly metres squared.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideHighErrorSimilarityThreshold"))
	float HighErrorSimilarityThreshold = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideHighErrorSimilarityThreshold = FLT_MAX;


	// MaxErrorTotalTime

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMaxErrorTotalTime = false;

	/// The maximum amount of time in seconds that the physics body is allowed to accrue errors that are not considered
	/// to be corrected before performing an immediate move to the remote physics state.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMaxErrorTotalTime", Units = "Seconds"))
	float MaxErrorTotalTime = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMaxErrorTotalTime = FLT_MAX;


	// MaxRemoteSleepIgnoreTime

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMaxRemoteSleepIgnoreTime = false;

	/// The maximum amount of time in seconds that the physics body will ignore remote state informing that the body should be sleeping.
	/// Higher values give local physics more time to try to reach the sleeping state before an immediate move is required.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMaxRemoteSleepIgnoreTime", Units = "Seconds"))
	float MaxRemoteSleepIgnoreTime = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMaxRemoteSleepIgnoreTime = FLT_MAX;


	// MaxExtrapolationTime

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMaxExtrapolationTime = false;

	/// Maximum extrapolation time clients are allowed to do. Higher values means extrapolating more.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMaxExtrapolationTime", Units = "Seconds"))
	float MaxExtrapolationTime = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMaxExtrapolationTime = FLT_MAX;


	// MaxSpawnExtrapolationTime

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideMaxSpawnExtrapolationTime = false;

	/// Maximum extrapolation time clients are allowed to do when objects are first spawned. Higher values means extrapolating more.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideMaxSpawnExtrapolationTime", Units = "Seconds"))
	float MaxSpawnExtrapolationTime = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideMaxSpawnExtrapolationTime = FLT_MAX;


	// DeltaRotationScale

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideDeltaRotationScale = false;

	/// Scale factor applied when extrapolating the delta rotation
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideDeltaRotationScale"))
	float DeltaRotationScale = FLT_MAX;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	float CachedOverrideDeltaRotationScale = FLT_MAX;


	// GravityForecast

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideGravityForecast = false;

	/// Whether we apply gravity to the forecast.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideGravityForecast"))
	EFusionGravityForecast GravityForecast = EFusionGravityForecast::Invalid;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	EFusionGravityForecast CachedOverrideGravityForecast = EFusionGravityForecast::Invalid;


	// ErrorCorrectionType

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (InlineEditConditionToggle))
	bool OverrideErrorCorrectionType = false;

	/// Correction type to be applied on the physics body. [Velocity] is recommended.
	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "OverrideErrorCorrectionType"))
	EFusionPhysicsCorrection ErrorCorrectionType = EFusionPhysicsCorrection::Invalid;

	UPROPERTY(EditAnywhere, Category = "Fusion|Forecast Physics", meta = (EditCondition = "false", EditConditionHides))
	EFusionPhysicsCorrection CachedOverrideErrorCorrectionType = EFusionPhysicsCorrection::Invalid;


	// End of settings that override those found in PhotonOnlineSubsystemSettings.h

};

