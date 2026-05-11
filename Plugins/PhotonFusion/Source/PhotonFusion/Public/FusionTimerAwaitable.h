// Copyright 2026 Exit Games GmbH. All Rights Reserved.

#pragma once

#include "Fusion/CoroutineCompat.h"
#include "Engine/World.h"
#include "UObject/WeakObjectPtr.h"

struct FusionTimerAwaitable {
    TWeakObjectPtr<UWorld> World;
    float Delay;
    FTimerHandle Handle;

    FusionTimerAwaitable(UWorld* InWorld, float InDelay)
        : World(InWorld), Delay(InDelay) {}

    bool await_ready() const noexcept { return false; }

    void await_suspend(std::coroutine_handle<> Continuation) {
        
		UWorld* W = World.Get();
        
		if (!W)
		{
            Continuation.resume();
            return;
        }

        W->GetTimerManager().SetTimer(Handle, FTimerDelegate::CreateLambda([Continuation]() mutable {
            Continuation.resume();
        }), Delay, false);
    }

    void await_resume() const noexcept {}
};
