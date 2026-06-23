// Owns the particle system preview world, actor, component, and playback driving.
#include "Editor/UI/EditorParticleSystemWidgetPrivate.h"

void FEditorParticleSystemWidget::EnsurePreviewViewport()
{
	if (bPreviewViewportInitialized || !EditorEngine)
	{
		return;
	}

	static int32 PreviewCounter = 0;
	const FString HandleText = "__ParticleSystemPreview_" + std::to_string(PreviewCounter++);
	PreviewWorldHandle = FName(HandleText.c_str());

	FWorldContext& PreviewContext = EditorEngine->CreateWorldContext(
		EWorldType::ViewerPreview,
		PreviewWorldHandle,
		"Particle System Preview");
	EditorEngine->ApplySpatialIndexMaintenanceSettings(PreviewContext.World);

	PreviewViewport.SetClient(&PreviewClient);
	PreviewClient.Initialize(EditorEngine->GetWindow(), EditorEngine);
	PreviewClient.SetWorld(PreviewContext.World);
	PreviewClient.SetGizmo(PreviewContext.SelectionManager ? PreviewContext.SelectionManager->GetGizmo() : nullptr);
	PreviewClient.SetSelectionManager(PreviewContext.SelectionManager);
	PreviewClient.SetSceneEditingShortcutsEnabled(false);
	PreviewClient.SetViewport(&PreviewViewport);
	PreviewClient.SetState(&PreviewViewport.GetState());
	PreviewClient.SetViewportType(EEditorViewportType::EVT_Perspective);
	PreviewClient.CreateCamera();
	PreviewClient.ApplyCameraMode();

	PreviewViewport.GetState().ViewMode = EViewMode::Lit_BlinnPhong;
	PreviewViewport.GetState().LightCullMode = ELightCullMode::None;

	const FViewportRect InitialRect(0, 0, 300, 300);
	PreviewViewport.SetRect(InitialRect);
	PreviewClient.SetViewportSize(static_cast<float>(InitialRect.Width), static_cast<float>(InitialRect.Height));

	if (UWorld* PreviewWorld = PreviewContext.World)
	{
		ADirectionalLightActor* DirectionalLight = PreviewWorld->SpawnActor<ADirectionalLightActor>();
		if (DirectionalLight)
		{
			DirectionalLight->InitDefaultComponents();
			DirectionalLight->SetFName(FName("Particle Preview Directional Light"));
			DirectionalLight->SetActorLocation(FVector(100000.0f, 100000.0f, 100000.0f));
			DirectionalLight->SetActorRotation(FVector(0.0f, 44.0f, 0.0f));
		}

		AAmbientLightActor* AmbientLight = PreviewWorld->SpawnActor<AAmbientLightActor>();
		if (AmbientLight)
		{
			AmbientLight->InitDefaultComponents();
			AmbientLight->SetFName(FName("Particle Preview Ambient Light"));
			AmbientLight->SetActorLocation(FVector(100000.0f, 100000.0f, 100000.0f));
			if (UAmbientLightComponent* AmbientComp = AmbientLight->FindComponent<UAmbientLightComponent>())
			{
				AmbientComp->Intensity = 0.7f;
			}
		}

		PreviewWorld->SyncSpatialIndex();
	}

	bPreviewViewportInitialized = true;
	EnsurePreviewActor();
}

void FEditorParticleSystemWidget::EnsurePreviewActor()
{
	if (PreviewComponent || !EditorEngine || PreviewWorldHandle == FName::None)
	{
		return;
	}

	FWorldContext* PreviewContext = EditorEngine->GetWorldContextFromHandle(PreviewWorldHandle);
	UWorld* PreviewWorld = PreviewContext ? PreviewContext->World : nullptr;
	if (!PreviewWorld)
	{
		return;
	}

	PreviewActor = PreviewWorld->SpawnActor<AActor>();
	if (!PreviewActor)
	{
		return;
	}

	PreviewActor->SetFName(FName("Particle Preview Actor"));
	// ViewerPreview worlds tick through the game path, so TickInEditor does not gate this actor.
	// Keep the preview actor inactive and drive its particle component explicitly from the editor UI.
	PreviewActor->SetActive(false);
	PreviewActor->SetActorLocation(FVector::ZeroVector);

	PreviewComponent = PreviewActor->AddComponent<UParticleSystemComponent>();
	if (!PreviewComponent)
	{
		PreviewWorld->DestroyActor(PreviewActor);
		PreviewActor = nullptr;
		return;
	}

	PreviewComponent->SetTransient(true);
	PreviewComponent->SetEditorOnly(true);
	PreviewActor->SetRootComponent(PreviewComponent);
	PreviewComponent->SetTemplate(ParticleSystemAsset);
	ApplyPreviewSoloEmitters();
	PreviewClient.SetFocusTargetActor(PreviewActor);
	PreviewWorld->SyncSpatialIndex();
}

void FEditorParticleSystemWidget::RefreshPreviewComponent(bool bRestartSimulation)
{
	EnsurePreviewViewport();
	EnsurePreviewActor();

	if (!PreviewComponent)
	{
		return;
	}

	if (ParticleSystemAsset)
	{
		SyncParticleDistributionRuntimeDataToAsset();
		if (!DocumentPath.empty())
		{
			FResourceManager::Get().RegisterParticleSystem(ParticleSystemAsset, DocumentPath);
		}
		ParticleSystemAsset->CacheEmitterModuleInfo();
	}

	if (PreviewComponent->GetTemplate() != ParticleSystemAsset)
	{
		PreviewComponent->SetTemplate(ParticleSystemAsset);
		ApplyPreviewSoloEmitters();
		RestartPreviewPlayback();
	}
	else
	{
		PreviewComponent->RefreshTemplateRuntime(bRestartSimulation);
		ApplyPreviewSoloEmitters();
		if (bRestartSimulation)
		{
			RestartPreviewPlayback();
		}
	}

	if (PreviewComponent->GetTotalActiveParticleCount() == 0 && !bPreviewPaused)
	{
		PreviewComponent->TickPreview(0.1f * GetPreviewAnimSpeed(), true);
	}

	RefreshPlacedParticleSystemComponents(bRestartSimulation);
	SyncPreviewWorld();
}

void FEditorParticleSystemWidget::RefreshPlacedParticleSystemComponents(bool bRestartSimulation)
{
	if (!EditorEngine || !ParticleSystemAsset)
	{
		return;
	}

	const FString EditedAssetPath = FPaths::Normalize(
		!DocumentPath.empty() ? DocumentPath : ParticleSystemAsset->GetAssetPath());

	for (FWorldContext& Context : EditorEngine->GetWorldList())
	{
		UWorld* World = Context.World;
		if (!World || Context.ContextHandle == PreviewWorldHandle)
		{
			continue;
		}

		bool bRefreshedAnyComponent = false;
		for (AActor* Actor : World->GetActors())
		{
			if (!Actor)
			{
				continue;
			}

			UParticleSystemComponent* ParticleComponent = Actor->FindComponent<UParticleSystemComponent>();
			if (!ParticleComponent || ParticleComponent == PreviewComponent)
			{
				continue;
			}

			UParticleSystem* ComponentTemplate = ParticleComponent->GetTemplate();
			const FString ComponentTemplatePath = FPaths::Normalize(ComponentTemplate ? ComponentTemplate->GetAssetPath() : FString());
			const FString ComponentSoftPath = FPaths::Normalize(ParticleComponent->GetTemplateAssetPath());
			const bool bSameTemplate = ComponentTemplate == ParticleSystemAsset;
			const bool bSameAssetPath =
				!EditedAssetPath.empty() &&
				(ComponentTemplatePath == EditedAssetPath || ComponentSoftPath == EditedAssetPath);
			if (!bSameTemplate && !bSameAssetPath)
			{
				continue;
			}

			if (!bSameTemplate)
			{
				ParticleComponent->SetTemplate(ParticleSystemAsset);
			}
			ParticleComponent->RefreshTemplateRuntime(bRestartSimulation);
			bRefreshedAnyComponent = true;
		}

		if (bRefreshedAnyComponent)
		{
			World->SyncSpatialIndex();
		}
	}
}

void FEditorParticleSystemWidget::SyncPreviewWorld()
{
	if (EditorEngine && PreviewWorldHandle != FName::None)
	{
		if (FWorldContext* PreviewContext = EditorEngine->GetWorldContextFromHandle(PreviewWorldHandle))
		{
			if (PreviewContext->World)
			{
				PreviewContext->World->SyncSpatialIndex();
			}
		}
	}
}

void FEditorParticleSystemWidget::SetPreviewBoundsVisible(bool bVisible)
{
	bShowBounds = bVisible;
	if (bPreviewViewportInitialized)
	{
		PreviewClient.GetParticleShowFlags().bBounds = bShowBounds;
	}
}

void FEditorParticleSystemWidget::SetPreviewOriginAxisVisible(bool bVisible)
{
	bShowOriginAxis = bVisible;
	if (bPreviewViewportInitialized)
	{
		PreviewClient.GetParticleShowFlags().bAxis = bShowOriginAxis;
	}
}

float FEditorParticleSystemWidget::GetPreviewAnimSpeed() const
{
	static constexpr float SpeedValues[] = { 1.0f, 0.5f, 0.25f, 0.1f, 0.01f };
	const int32 ClampedIndex = std::clamp(PreviewAnimSpeedIndex, 0, static_cast<int32>(IM_ARRAYSIZE(SpeedValues)) - 1);
	return SpeedValues[ClampedIndex];
}

float FEditorParticleSystemWidget::GetPreviewMaxEmitterDuration() const
{
	float MaxDuration = 0.0f;
	if (!ParticleSystemAsset)
	{
		return MaxDuration;
	}

	for (const UParticleEmitter* Emitter : ParticleSystemAsset->GetEmitters())
	{
		const UParticleLODLevel* LODLevel = Emitter ? Emitter->GetLODLevel(CurrentLOD) : nullptr;
		const UParticleModuleRequired* Required = LODLevel ? LODLevel->GetRequiredModule() : nullptr;
		if (Required)
		{
			MaxDuration = std::max(MaxDuration, Required->GetEmitterDuration());
		}
	}
	return MaxDuration;
}

void FEditorParticleSystemWidget::RestartPreviewPlayback()
{
	PreviewPlaybackElapsed = 0.0f;
	bPreviewPlaybackComplete = false;
}

void FEditorParticleSystemWidget::DrivePreviewPlayback(float DeltaTime)
{
	if (!PreviewComponent || bPreviewPaused || bPreviewPlaybackComplete || DeltaTime <= 0.0f)
	{
		return;
	}

	const float PreviewDeltaTime = DeltaTime * GetPreviewAnimSpeed();
	if (bPreviewLoop)
	{
		PreviewComponent->TickPreview(PreviewDeltaTime, true);
		PreviewPlaybackElapsed += PreviewDeltaTime;
		SyncPreviewWorld();
		return;
	}

	const float EmitterDuration = GetPreviewMaxEmitterDuration();
	const bool bCanSpawnAtStart = PreviewPlaybackElapsed < EmitterDuration;
	const float SpawnTimeRemaining = std::max(0.0f, EmitterDuration - PreviewPlaybackElapsed);
	const float SpawnDeltaTime = bCanSpawnAtStart ? std::min(PreviewDeltaTime, SpawnTimeRemaining) : 0.0f;
	const float UpdateOnlyDeltaTime = PreviewDeltaTime - SpawnDeltaTime;

	if (SpawnDeltaTime > 0.0f)
	{
		PreviewComponent->TickPreview(SpawnDeltaTime, true);
		PreviewPlaybackElapsed += SpawnDeltaTime;
	}
	if (UpdateOnlyDeltaTime > 0.0f)
	{
		PreviewComponent->TickPreview(UpdateOnlyDeltaTime, false);
		PreviewPlaybackElapsed += UpdateOnlyDeltaTime;
	}

	if (PreviewPlaybackElapsed >= EmitterDuration && PreviewComponent->GetTotalActiveParticleCount() == 0)
	{
		bPreviewPlaybackComplete = true;
		bPreviewPaused = true;
	}
	SyncPreviewWorld();
}

void FEditorParticleSystemWidget::ShutdownPreviewViewport()
{
	bPreviewViewportVisible = false;
	bPreviewViewportRectValid = false;
	PreviewComponent = nullptr;
	PreviewClient.SetFocusTargetActor(nullptr);
	PreviewActor = nullptr;

	PreviewClient.DestroyCamera();
	PreviewClient.SetWorld(nullptr);
	PreviewViewport.SetClient(nullptr);
	PreviewViewport.SetRenderTargetSet(nullptr);

	if (EditorEngine && PreviewWorldHandle != FName::None && EditorEngine->GetWorldContextFromHandle(PreviewWorldHandle))
	{
		EditorEngine->UnregisterWorld(PreviewWorldHandle);
	}

	PreviewWorldHandle = FName::None;
	bPreviewViewportInitialized = false;
}
