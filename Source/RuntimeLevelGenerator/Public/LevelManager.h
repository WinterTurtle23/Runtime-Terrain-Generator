// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ProceduralMeshComponent.h"
#include "LevelManager.generated.h"

UCLASS()
class RUNTIMELEVELGENERATOR_API ALevelManager : public AActor
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	ALevelManager();

	UFUNCTION(BlueprintCallable, Category="Terrain")
	bool ImportAndCreateTerrain(const FString& FilePath);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	float HorizontalScale = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="Terrain")
	float VerticalScale = 1.0f;

	// Create a Folder in which all the saved terrains will be stored
	UFUNCTION(BlueprintCallable, Category="RuntimeLevel")
	void EnsureRTLevelsFolderExists();

	// Returns simple list of saved map folder names (folders that contain height_16bit.png)
	UFUNCTION(BlueprintCallable, Category="RuntimeLevel")
	TArray<FString> GetSavedLevelNames() const;

	// Save currently loaded heightmap into Saved/MyRTLevels/<LevelName>/
	UFUNCTION(BlueprintCallable, Category="RuntimeLevel")
	bool SaveRuntimeLevel(const FString& LevelName);

	// Load a saved runtime level by name (reads height_16bit.png from Saved/MyRTLevels/<LevelName>/)
	UFUNCTION(BlueprintCallable, Category="RuntimeLevel")
	bool LoadRuntimeLevel(const FString& LevelName);

	UFUNCTION(BlueprintCallable, Category="RuntimeLevel")
	bool DeleteRuntimeLevel(const FString& LevelName);

	UFUNCTION(BlueprintCallable, Category="RuntimeLevel")
	FString OpenHeightmapFileDialog();

	// Helper to write 16-bit PNG (used by SaveRuntimeLevel)
	bool SaveRawToPNG16(const FString& FullPath, const TArray<uint16>& Heights, int32 W, int32 H) const;

	UPROPERTY()
	TArray<uint16> RawHeights;

	UPROPERTY()
	int32 ImgWidth = 0;

	UPROPERTY()
	int32 ImgHeight = 0;

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

public:
	// Called every frame
	virtual void Tick(float DeltaTime) override;

private:
	bool LoadPNG16ToRaw(const FString& FullPath, TArray<uint16>& OutHeights, int32& W, int32& H);
	void BuildTerrain(const TArray<uint16>& Heights, int32 W, int32 H);

	UProceduralMeshComponent* MeshComp;
};
