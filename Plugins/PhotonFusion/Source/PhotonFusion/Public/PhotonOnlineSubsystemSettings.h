// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "PhotonOnlineSubsystemSettings.generated.h"

UENUM(BlueprintType)
enum class EFusionGravityForecast : uint8
{
	Invalid = 0 UMETA(Hidden),

	/// Applies gravity to the forecast
	Apply = 1,

	/// Does not apply gravity to the forecast
	None = 2,

	/// Applies gravity if the remote body has downward velocity 
	Auto = 3,
};

UENUM(BlueprintType)
enum class EFusionPhysicsCorrection : uint8
{
	Invalid = 0 UMETA(Hidden),

	/// Handle the error by applying a correction velocity that will move the body toward the predicted position.
	/// Rotation is handled the same way as <see cref="PhysicsCorrection.PositionRotation"/>.
	Velocity = 1,

	/// Handle the error by directly applying a calculated target position and rotation into the physics body, towards the predicted position and rotation.
	PositionRotation = 2,

	/// Handle the error by using a spring damping logic to calculate and add force/torque to correct the physics body toward the predicted position and rotation.
	SpringDamping = 3,
};

UENUM(Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESharedModeLogLevels : uint8
{
	Trace = 1 << 0,
	Debug = 1 << 1,
	Info = 1 << 2,
	Warning = 1 << 3,
	Error = 1 << 4
};
ENUM_CLASS_FLAGS(ESharedModeLogLevels);

UENUM(Meta = (Bitflags, UseEnumValuesAsMaskValuesInEditor = "true"))
enum class ESharedModeLogOutput : uint8
{
	StandardLogOutput = 1 << 0,
	OnScreenDebugMessageLogOutput = 1 << 1,
};
ENUM_CLASS_FLAGS(ESharedModeLogOutput);

USTRUCT(BlueprintType)
struct FCustomProperty
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion", meta = (AllowAbstract = "true"))
	TSoftClassPtr<UObject> Type;
	
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Fusion")
	FName Name;
};

UCLASS(config=Game, defaultconfig, meta = (DisplayName = "Fusion Settings"))
class PHOTONFUSION_API UPhotonOnlineSubsystemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UPROPERTY(config, BlueprintReadOnly, EditAnywhere, Category = "Photon")
	FString AppId = "11111111-1111-1111-1111-111111111111";

	UPROPERTY(config, BlueprintReadOnly, EditAnywhere, Category = "Photon")
	FString AppVersion = "1.0";
	
	UPROPERTY(config, BlueprintReadOnly, EditAnywhere, Category = "Photon")
	FString LocalServerUrl = "127.0.0.1:5055";

	UPROPERTY(config, BlueprintReadOnly, EditAnywhere, Category = "Photon")
	FString CloudServerUrl = "";
	
	UPROPERTY(config, BlueprintReadOnly, EditAnywhere, Category = "Photon")
	int32 MinPlayers = 2;
	
	UPROPERTY(config, BlueprintReadOnly, EditAnywhere, Category = "Photon")
	int32 MaxPlayers = 8;

	UPROPERTY(Config, BlueprintReadWrite, EditAnywhere, Category = "Photon")
	bool UseLocalServer {false};
	
	UFUNCTION(BlueprintPure, Category = "Photon|Settings")
	static const UPhotonOnlineSubsystemSettings* GetPhotonOnlineSettings();

	UPROPERTY(Config, BlueprintReadWrite, EditAnywhere, Category = "Fusion")
	bool LoadMapAutomatically {true};

	UPROPERTY(Config, EditAnywhere, Category = "Logging", Meta = (Bitmask, BitmaskEnum = "/Script/PhotonFusion.ESharedModeLogLevels"))
	uint8 EnabledLogLevels = uint8(ESharedModeLogLevels::Debug | ESharedModeLogLevels::Info | ESharedModeLogLevels::Warning | ESharedModeLogLevels::Error);

	UPROPERTY(Config, EditAnywhere, Category = "Logging", Meta = (Bitmask, BitmaskEnum = "/Script/PhotonFusion.ESharedModeLogOutput"))
	uint8 EnabledLogOutput = uint8(ESharedModeLogOutput::StandardLogOutput);

	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Arrays")
	int32 DefaultArraySize = 16;

	/// A scale factor applied to the correction velocity if [Velocity] or [PositionRotation] is the [Error Correction Type].
	/// Higher values means the body will be corrected faster, but can increase overshoot
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float LinearVelCorrectionMul = 4.0f;

	/// A scale factor used when extrapolating the angular velocity. Higher values will result in a bigger extrapolated velocity.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float AngularVelCorrectionMul = 1.0f;

	/// How long after an impact do we apply a correction. Between the impact and this time no local correction is applied.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Seconds"))
	float ImpactStartCorrectionTime = 0.1f;

	/// After 'ImpactStartCorrectionTime' we lerp from no correction to normal correction over this time.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Seconds"))
	float ImpactCorrectionTimeComplete = 0.3f;

	/// Lerp object position from local to extrapolated (Range 0..1). Only used if the Error Correction Type is [Position Rotation]. 
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float PositionCorrectionLerp = .15f;

	/// Lerp object rotation from local to extrapolated (Range 0..1)
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float RotationCorrectionLerp = .15f;

	/// The intensity of the error correction, increase this value to make the error be corrected faster. If overshooting try increasing "Damper".
	/// Only used if the Error Correction Type is [Spring Damping].
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float Spring = 75.0f;

	/// The resistance of the correction oscillation, increase this value to increase the resistance of the spring correction and control overshooting.
	/// Keep it bellow "Spring" value.
	/// Only used if the Error Correction Type is [Spring Damping].
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float Damper = 2.0f;

	/// Error must be over this value (metres) for correction to be applied. Errors below this threshold are not considered
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Centimeters"))
	float MinLinearDetectedError = 2.0f;

	/// Error must be over this value(degrees) for correction to be applied. Errors below this threshold are not considered
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Degrees"))
	float MinAngularDetectedError = 3.0f;

	/// Linear error above this threshold will force an immediate move to the remote physics state. Zero means no limit.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Centimeters"))
	float MaxLinearError = 0.0f;

	/// Angular error above this threshold will force a immediate move to the remote physics state. Zero means no limit.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Degrees"))
	float MaxAngularError = 0.0f;

	/// If correction progress is below this accrue time towards a reset, in cm.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float LowCorrectionProgressThreshold = 1.5f;

	/// If current error direction and magnitude dot previous error direction and magnitude is above this accrue time 
	/// towards a reset. This value roughly represents how little the direction of the linear error has changed, and how 
	/// big the error is. Roughly metres squared.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float HighErrorSimilarityThreshold = 500.0f;

	/// The maximum amount of time in seconds that the physics body is allowed to accrue errors that are not considered
	/// to be corrected before performing an immediate move to the remote physics state.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Seconds"))
	float MaxErrorTotalTime = 1.0f;

	/// The maximum amount of time in seconds that the physics body will ignore remote state informing that the body should be sleeping.
	/// Higher values give local physics more time to try to reach the sleeping state before an immediate move is required.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Seconds"))
	float MaxRemoteSleepIgnoreTime = 2.0f;

	/// Maximum extrapolation time clients are allowed to do. Higher values means extrapolating more.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Seconds"))
	float MaxExtrapolationTime = 0.3f;

	/// Maximum extrapolation time clients are allowed to do when objects are first spawned. Higher values means extrapolating more.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics", meta = (Units = "Seconds"))
	float MaxSpawnExtrapolationTime = 0.05f;

	/// Scale factor applied when extrapolating the delta rotation
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	float DeltaRotationScale = 0.0174532924f;

	/// Whether we apply gravity to the forecast.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	EFusionGravityForecast GravityForecast = EFusionGravityForecast::Auto;

	/// Correction type to be applied on the physics body. [Velocity] is recommended.
	UPROPERTY(Config, EditAnywhere, Category = "Fusion|Forecast Physics")
	EFusionPhysicsCorrection ErrorCorrectionType = EFusionPhysicsCorrection::Velocity;

};