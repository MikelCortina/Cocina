// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#include "Fusion/LogUtils.h"
#include "HAL/IConsoleManager.h"
#include <FusionShared.h>

FAutoConsoleCommandWithWorldAndArgs GCmdPhotonLogEnable(
	TEXT("Photon.Logging.LogEnable"),
	TEXT("Enable a Photon log level (trace, debug, info, warning, error)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, [[maybe_unused]] UWorld* World)
		{
			if (Args.Num() > 0)
			{
				const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> LogLevelString = StringCast<UTF8CHAR>(*Args[0]);
				if (PhotonCommon::LogLevel LogLevelLocal; PhotonCommon::TryGetLogLevelFromString(reinterpret_cast<const PhotonCommon::CharType*>(LogLevelString.Get()), LogLevelLocal))
				{
					PhotonCommon::LogEnable(LogLevelLocal);
				}
				else
				{
					FUSION_LOG_WARN("Log level not supported: '%s'", *Args[0]);
				}
			}
		}
	));

FAutoConsoleCommandWithWorldAndArgs GCmdPhotonLogDisable(
	TEXT("Photon.Logging.LogDisable"),
	TEXT("Disable a Photon log level (trace, debug, info, warning, error)"),
	FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
		[](const TArray<FString>& Args, [[maybe_unused]] UWorld* World)
		{
			if (Args.Num() > 0)
			{
				const TStringConversion<TStringConvert<TCHAR, UTF8CHAR>> LogLevelString = StringCast<UTF8CHAR>(*Args[0]);
				if (PhotonCommon::LogLevel LogLevelLocal; PhotonCommon::TryGetLogLevelFromString(reinterpret_cast<const PhotonCommon::CharType*>(LogLevelString.Get()), LogLevelLocal))
				{
					PhotonCommon::LogDisable(LogLevelLocal);
				}
				else
				{
					FUSION_LOG_WARN("Log level not supported: '%s'", *Args[0]);
				}
			}
		}
	));

FAutoConsoleCommand GCmdPhotonLogLogLevels(
	TEXT("Photon.Logging.LogLogLevels"),
	TEXT("Log Log Levels to the info log level"),
	FConsoleCommandDelegate::CreateLambda(
		[]
		{
			PhotonCommon::LogLogLevels();
		}
	));
