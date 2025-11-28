// Fill out your copyright notice in the Description page of Project Settings.


#include "LevelManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "HAL/PlatformFileManager.h"
#include "Dom/JsonObject.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonSerializer.h"


// Sets default values
ALevelManager::ALevelManager()
{
 	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	MeshComp = CreateDefaultSubobject<UProceduralMeshComponent>(TEXT("TerrainMesh"));
	RootComponent = MeshComp;
	MeshComp->bUseAsyncCooking = true;
}

bool ALevelManager::ImportAndCreateTerrain(const FString& FilePath)
{
	FString Full = FilePath;
	if (!FPaths::FileExists(Full))
	{
		Full = FPaths::Combine(FPaths::ProjectSavedDir(), FilePath);
		if (!FPaths::FileExists(Full))
		{
			UE_LOG(LogTemp, Error, TEXT("File not found: %s"), *FilePath);
			return false;
		}
	}

	TArray<uint16> Heights;
	int32 W = 0, H = 0;

	if (!LoadPNG16ToRaw(Full, Heights, W, H))
	{
		UE_LOG(LogTemp, Error, TEXT("Failed to load heightmap."));
		return false;
	}

	// store into members so SaveRuntimeLevel can access them later
	RawHeights = MoveTemp(Heights);
	ImgWidth = W;
	ImgHeight = H;

	BuildTerrain(RawHeights, ImgWidth, ImgHeight);
	return true;
}

void ALevelManager::EnsureRTLevelsFolderExists()
{
	FString RootFolder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MyRTLevels"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	// If folder does NOT exist → create it
	if (!PlatformFile.DirectoryExists(*RootFolder))
	{
		bool bCreated = PlatformFile.CreateDirectoryTree(*RootFolder);

		if (bCreated)
		{
			UE_LOG(LogTemp, Log, TEXT("Created MyRTLevels folder at: %s"), *RootFolder);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("Failed to create MyRTLevels folder at: %s"), *RootFolder);
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("MyRTLevels folder already exists: %s"), *RootFolder);
	}
}

TArray<FString> ALevelManager::GetSavedLevelNames() const
{
	TArray<FString> Result;
	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MyRTLevels"));

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
	if (!PlatformFile.DirectoryExists(*Root))
	{
		return Result;
	}

	class FVisitor : public IPlatformFile::FDirectoryVisitor
	{
	public:
		TArray<FString>& Out;
		FVisitor(TArray<FString>& InOut) : Out(InOut) {}
		virtual bool Visit(const TCHAR* Path, bool bIsDirectory) override
		{
			if (!bIsDirectory) return true;
			FString Folder(Path);
			FString Img = FPaths::Combine(Folder, TEXT("height_16bit.png"));
			if (FPaths::FileExists(Img))
			{
				Out.Add(FPaths::GetCleanFilename(Folder));
			}
			return true;
		}
	};

	FVisitor Visitor(Result);
	PlatformFile.IterateDirectory(*Root, Visitor);
	Result.Sort();
	return Result;
}

bool ALevelManager::SaveRuntimeLevel(const FString& LevelName)
{
	if (LevelName.IsEmpty())
    {
        UE_LOG(LogTemp, Error, TEXT("SaveRuntimeLevel: empty LevelName"));
        return false;
    }

    if (RawHeights.Num() == 0 || ImgWidth <= 0 || ImgHeight <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("SaveRuntimeLevel: no loaded heightmap to save"));
        return false;
    }

    const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MyRTLevels"));
    const FString Folder = FPaths::Combine(Root, LevelName);

    IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();
    if (!PlatformFile.DirectoryExists(*Folder))
    {
        if (!PlatformFile.CreateDirectoryTree(*Folder))
        {
            UE_LOG(LogTemp, Error, TEXT("SaveRuntimeLevel: Failed to create folder: %s"), *Folder);
            return false;
        }
    }

    // Save heightmap PNG
    const FString ImgPath = FPaths::Combine(Folder, TEXT("height_16bit.png"));
    if (!SaveRawToPNG16(ImgPath, RawHeights, ImgWidth, ImgHeight))
    {
        UE_LOG(LogTemp, Error, TEXT("SaveRuntimeLevel: Failed to save image: %s"), *ImgPath);
        return false;
    }

    // Build metadata JSON manually
    TSharedPtr<FJsonObject> JsonObj = MakeShared<FJsonObject>();
    JsonObj->SetStringField(TEXT("LevelName"), LevelName);
    JsonObj->SetNumberField(TEXT("Width"), ImgWidth);
    JsonObj->SetNumberField(TEXT("Height"), ImgHeight);
    JsonObj->SetNumberField(TEXT("HorizontalScale"), HorizontalScale);
    JsonObj->SetNumberField(TEXT("VerticalScale"), VerticalScale);

    // Serialize to string
    FString OutputString;
    TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutputString);
    if (!FJsonSerializer::Serialize(JsonObj.ToSharedRef(), Writer))
    {
        UE_LOG(LogTemp, Error, TEXT("SaveRuntimeLevel: Failed to serialize metadata JSON"));
        return false;
    }
    Writer->Close();

    // Save metadata file
    const FString MetaPath = FPaths::Combine(Folder, TEXT("metadata.json"));
    if (!FFileHelper::SaveStringToFile(OutputString, *MetaPath))
    {
        UE_LOG(LogTemp, Error, TEXT("SaveRuntimeLevel: Failed to save metadata.json to %s"), *MetaPath);
        return false;
    }

    UE_LOG(LogTemp, Log, TEXT("SaveRuntimeLevel: Saved map '%s' to %s"), *LevelName, *Folder);
    return true;
}

bool ALevelManager::LoadRuntimeLevel(const FString& LevelName)
{
	if (LevelName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("LoadRuntimeLevel: empty LevelName"));
		return false;
	}

	const FString Folder = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MyRTLevels"), LevelName);
	const FString ImgPath = FPaths::Combine(Folder, TEXT("height_16bit.png"));
	const FString MetaPath = FPaths::Combine(Folder, TEXT("metadata.json"));

	if (!FPaths::FileExists(ImgPath))
	{
		UE_LOG(LogTemp, Error, TEXT("LoadRuntimeLevel: heightmap not found: %s"), *ImgPath);
		return false;
	}

	// Decode the PNG into a temporary array
	TArray<uint16> Loaded;
	int32 W = 0, H = 0;
	if (!LoadPNG16ToRaw(ImgPath, Loaded, W, H))
	{
		UE_LOG(LogTemp, Error, TEXT("LoadRuntimeLevel: Failed to decode PNG: %s"), *ImgPath);
		return false;
	}

	// store into members
	RawHeights = MoveTemp(Loaded);
	ImgWidth = W;
	ImgHeight = H;

	// Clear previous mesh sections (if any) before building new terrain
	if (MeshComp)
	{
		MeshComp->ClearAllMeshSections();
	}

	// Build new terrain from loaded data
	BuildTerrain(RawHeights, ImgWidth, ImgHeight);

	UE_LOG(LogTemp, Log, TEXT("LoadRuntimeLevel: Successfully loaded '%s' (%dx%d)"), *LevelName, ImgWidth, ImgHeight);
	return true;
}

bool ALevelManager::DeleteRuntimeLevel(const FString& LevelName)
{
	if (LevelName.IsEmpty())
	{
		UE_LOG(LogTemp, Error, TEXT("DeleteRuntimeLevel: empty LevelName"));
		return false;
	}

	const FString Root = FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("MyRTLevels"));
	const FString Folder = FPaths::Combine(Root, LevelName);

	IPlatformFile& PlatformFile = FPlatformFileManager::Get().GetPlatformFile();

	if (!PlatformFile.DirectoryExists(*Folder))
	{
		UE_LOG(LogTemp, Warning, TEXT("DeleteRuntimeLevel: folder does not exist: %s"), *Folder);
		return false;
	}

	// Recursively delete folder
	bool bDeleted = PlatformFile.DeleteDirectoryRecursively(*Folder);
	if (bDeleted)
	{
		UE_LOG(LogTemp, Log, TEXT("DeleteRuntimeLevel: deleted %s"), *Folder);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DeleteRuntimeLevel: failed to delete %s"), *Folder);
	}

	return bDeleted;
}

// Windows native file dialog
#if PLATFORM_WINDOWS
	#ifndef WIN32_LEAN_AND_MEAN
	#define WIN32_LEAN_AND_MEAN
	#endif
	#include <Windows.h>
	#include <commdlg.h>
#endif

FString ALevelManager::OpenHeightmapFileDialog()
{
#if PLATFORM_WINDOWS
	// Filter string (double-null terminated)
	const wchar_t* Filter = L"PNG files (*.png)\0*.png\0All files (*.*)\0*.*\0\0";

	wchar_t FileBuffer[MAX_PATH] = { 0 };
	OPENFILENAMEW Ofn;
	ZeroMemory(&Ofn, sizeof(Ofn));
	Ofn.lStructSize = sizeof(Ofn);

	// Do NOT depend on engine window pointers; packaging builds may not expose them.
	// Use the active foreground window as owner (optional) — safe in packaged runtime.
	HWND OwnerHwnd = GetActiveWindow();
	Ofn.hwndOwner = OwnerHwnd;

	Ofn.lpstrFile = FileBuffer;
	Ofn.nMaxFile = MAX_PATH;
	Ofn.lpstrFilter = Filter;
	Ofn.nFilterIndex = 1;
	Ofn.Flags = OFN_PATHMUSTEXIST | OFN_FILEMUSTEXIST | OFN_NOCHANGEDIR;

	BOOL bResult = GetOpenFileNameW(&Ofn);
	if (bResult == TRUE)
	{
		return FString(FileBuffer);
	}

	// cancelled or failed
	return FString();
#else
	UE_LOG(LogTemp, Warning, TEXT("OpenHeightmapFileDialog: Not supported on this platform"));
	return FString();
#endif
}

bool ALevelManager::SaveRawToPNG16(const FString& FullPath, const TArray<uint16>& Heights, int32 W, int32 H) const
{
	if (Heights.Num() != W * H)
	{
		UE_LOG(LogTemp, Error, TEXT("SaveRawToPNG16: Size mismatch"));
		return false;
	}

	// Create wrapper
	IImageWrapperModule& ImgModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
	TSharedPtr<IImageWrapper> Wrapper = ImgModule.CreateImageWrapper(EImageFormat::PNG);
	if (!Wrapper.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("SaveRawToPNG16: Wrapper invalid"));
		return false;
	}

	// Build raw image bytes: [lo,hi][lo,hi]...
	TArray<uint8> RawBytes;
	RawBytes.SetNumUninitialized(W * H * 2);

	for (int32 i = 0; i < W * H; ++i)
	{
		uint16 v = Heights[i];
		RawBytes[2 * i]     = (uint8)(v & 0xFF);
		RawBytes[2 * i + 1] = (uint8)((v >> 8) & 0xFF);
	}

	// SetRaw: grayscale, 16-bit
	if (!Wrapper->SetRaw(RawBytes.GetData(), RawBytes.Num(), W, H, ERGBFormat::Gray, 16))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveRawToPNG16: SetRaw failed"));
		return false;
	}

	// Get compressed PNG (UE5 may return TArray64<uint8>)
	auto CompressedTemp = Wrapper->GetCompressed(100);

	// Copy into TArray<uint8> for SaveArrayToFile
	TArray<uint8> Compressed;
	Compressed.SetNumUninitialized((int32)CompressedTemp.Num());

	for (int64 i = 0; i < CompressedTemp.Num(); ++i)
	{
		Compressed[(int32)i] = (uint8)CompressedTemp[i];
	}

	// Save to disk
	if (!FFileHelper::SaveArrayToFile(Compressed, *FullPath))
	{
		UE_LOG(LogTemp, Error, TEXT("SaveRawToPNG16: Failed to write %s"), *FullPath);
		return false;
	}

	return true;
}

// Called when the game starts or when spawned
void ALevelManager::BeginPlay()
{
	Super::BeginPlay();

	EnsureRTLevelsFolderExists();
	
}

// Called every frame
void ALevelManager::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

bool ALevelManager::LoadPNG16ToRaw(const FString& FullPath, TArray<uint16>& OutHeights, int32& W, int32& H)
{
	TArray<uint8> Bytes;
	if (!FFileHelper::LoadFileToArray(Bytes, *FullPath))
		return false;

	IImageWrapperModule& ImgWrap = FModuleManager::LoadModuleChecked<IImageWrapperModule>("ImageWrapper");
	TSharedPtr<IImageWrapper> Wrapper = ImgWrap.CreateImageWrapper(EImageFormat::PNG);

	if (!Wrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
		return false;

	W = Wrapper->GetWidth();
	H = Wrapper->GetHeight();

	TArray<uint8> Raw;
	if (!Wrapper->GetRaw(ERGBFormat::Gray, 16, Raw))
		return false;

	OutHeights.SetNum(W * H);

	for (int32 i = 0; i < W * H; i++)
	{
		uint8 lo = Raw[2*i];
		uint8 hi = Raw[2*i + 1];
		OutHeights[i] = (hi << 8) | lo;
	}

	return true;
}

void ALevelManager::BuildTerrain(const TArray<uint16>& Heights, int32 W, int32 H)
{
	TArray<FVector> Verts;
	TArray<int32> Tris;
	TArray<FVector> Normals;
	TArray<FVector2D> UVs;
	TArray<FLinearColor> Colors;
	TArray<FProcMeshTangent> Tans;

	Verts.Reserve(W * H);
	UVs.Reserve(W * H);

	for (int32 y = 0; y < H; y++)
	{
		for (int32 x = 0; x < W; x++)
		{
			float h = (float)Heights[y * W + x] * VerticalScale;
			Verts.Add(FVector(x * HorizontalScale, y * HorizontalScale, h));
			UVs.Add(FVector2D((float)x / W, (float)y / H));
		}
	}

	for (int32 y = 0; y < H - 1; y++)
	{
		for (int32 x = 0; x < W - 1; x++)
		{
			int i0 = y * W + x;
			int i1 = i0 + 1;
			int i2 = i0 + W;
			int i3 = i2 + 1;

			Tris.Add(i0); Tris.Add(i2); Tris.Add(i1);
			Tris.Add(i1); Tris.Add(i2); Tris.Add(i3);
		}
	}

	Normals.Init(FVector::UpVector, Verts.Num()); // simple normals

	MeshComp->CreateMeshSection_LinearColor(0, Verts, Tris, Normals, UVs, Colors, Tans, true);
}

