#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FluidBenchmarkComponent.generated.h"

USTRUCT(BlueprintType)
struct FBenchmarkResult
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	FString TestName;

	UPROPERTY(BlueprintReadOnly)
	float AverageFrameTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MinFrameTime = FLT_MAX;

	UPROPERTY(BlueprintReadOnly)
	float MaxFrameTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float SimulationTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MeshGenerationTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float BorderSyncTime = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 ActiveChunks = 0;

	UPROPERTY(BlueprintReadOnly)
	int32 ActiveCells = 0;

	UPROPERTY(BlueprintReadOnly)
	float TotalFluidVolume = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	float MemoryUsageMB = 0.0f;

	UPROPERTY(BlueprintReadOnly)
	int32 SampleCount = 0;

	FString ToString() const
	{
		return FString::Printf(
			TEXT("%s:\n")
			TEXT("  Frame: %.2fms (min: %.2fms, max: %.2fms)\n")
			TEXT("  Simulation: %.2fms, Mesh: %.2fms, Border: %.2fms\n")
			TEXT("  Chunks: %d, Cells: %d, Volume: %.1f\n")
			TEXT("  Memory: %.1f MB\n")
			TEXT("  Samples: %d"),
			*TestName,
			AverageFrameTime, MinFrameTime, MaxFrameTime,
			SimulationTime, MeshGenerationTime, BorderSyncTime,
			ActiveChunks, ActiveCells, TotalFluidVolume,
			MemoryUsageMB,
			SampleCount
		);
	}
};

USTRUCT(BlueprintType)
struct FBenchmarkConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	FString ConfigName = TEXT("Default");

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUseSleepChains = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bUsePredictiveSettling = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	bool bEnableMemoryCompression = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 ChunkSize = 32;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float CellSize = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 MaxActiveChunks = 64;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	float FluidSpawnAmount = 1000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite)
	int32 FluidSourceCount = 5;
};

UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VOXELFLUIDSYSTEM_API UFluidBenchmarkComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFluidBenchmarkComponent();

	// Benchmark Controls
	UFUNCTION(BlueprintCallable, Category = "Benchmark", meta = (CallInEditor = "true"))
	void StartBenchmark();

	UFUNCTION(BlueprintCallable, Category = "Benchmark", meta = (CallInEditor = "true"))
	void StopBenchmark();

	UFUNCTION(BlueprintCallable, Category = "Benchmark", meta = (CallInEditor = "true"))
	void RunComparisonBenchmark();

	UFUNCTION(BlueprintCallable, Category = "Benchmark", meta = (CallInEditor = "true"))
	void SaveBenchmarkResults();

	UFUNCTION(BlueprintCallable, Category = "Benchmark", meta = (CallInEditor = "true"))
	void ClearResults();

	// Quick Tests
	UFUNCTION(BlueprintCallable, Category = "Benchmark|Quick", meta = (CallInEditor = "true"))
	void QuickTestNoOptimizations();

	UFUNCTION(BlueprintCallable, Category = "Benchmark|Quick", meta = (CallInEditor = "true"))
	void QuickTestWithOptimizations();

	UFUNCTION(BlueprintCallable, Category = "Benchmark|Quick", meta = (CallInEditor = "true"))
	void QuickTestMemoryCompression();

	// Stress Tests
	UFUNCTION(BlueprintCallable, Category = "Benchmark|Stress", meta = (CallInEditor = "true"))
	void StressTest128Resolution();

	UFUNCTION(BlueprintCallable, Category = "Benchmark|Stress", meta = (CallInEditor = "true"))
	void StressTest256Resolution();

	UFUNCTION(BlueprintCallable, Category = "Benchmark|Stress", meta = (CallInEditor = "true"))
	void StressTest512Resolution();

	// Results
	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	FString GetResultsReport() const;

	UFUNCTION(BlueprintCallable, Category = "Benchmark")
	FString GetComparisonReport() const;

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// Benchmark Settings
	UPROPERTY(EditAnywhere, Category = "Settings")
	float BenchmarkDuration = 10.0f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	float WarmupTime = 2.0f;

	UPROPERTY(EditAnywhere, Category = "Settings")
	bool bAutoSaveResults = true;

	UPROPERTY(EditAnywhere, Category = "Settings")
	FString ResultsFilePath = TEXT("Saved/Benchmarks/");

	// Test Configurations
	UPROPERTY(EditAnywhere, Category = "Configurations")
	TArray<FBenchmarkConfig> TestConfigs;

	// Results Storage
	UPROPERTY(VisibleAnywhere, Category = "Results")
	TArray<FBenchmarkResult> BenchmarkResults;

	UPROPERTY(VisibleAnywhere, Category = "Results")
	FBenchmarkResult CurrentResult;

	// Runtime State
	bool bIsBenchmarking = false;
	float BenchmarkTimer = 0.0f;
	float WarmupTimer = 0.0f;
	int32 CurrentConfigIndex = 0;
	bool bInWarmup = false;

	// Helper Functions
	void ApplyConfiguration(const FBenchmarkConfig& Config);
	void RestoreOriginalConfiguration();
	void CollectSample(float DeltaTime);
	void FinalizeBenchmark();
	void RunNextConfiguration();
	FString GenerateCSVReport() const;
	float CalculateMemoryUsage() const;

	// Original settings storage
	FBenchmarkConfig OriginalConfig;
	class AVoxelFluidActor* FluidActor = nullptr;
};