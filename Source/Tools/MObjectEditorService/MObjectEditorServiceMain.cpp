#include "Tools/MObjectEditorService/App/MObjectEditorServiceApp.h"
#include "Tools/MObjectEditorService/Core/EditorPaths.h"
#include "Tools/MObjectEditorService/Http/EditorApiController.h"
#include "Tools/MObjectEditorService/Http/HttpServer.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <iostream>
#include <thread>

namespace
{
std::atomic<bool> GKeepRunning{true};

void PrintUsage()
{
    std::cout
        << "Usage:\n"
        << "  MObjectEditorService --new-monster <AssetName> [--category <CategoryPath>] [--save]\n"
        << "  MObjectEditorService --open <SourcePath> [--validate] [--export] [--publish] [--roundtrip] [--save]\n"
        << "  MObjectEditorService --serve [--port <Port>] [--open <SourcePath>]\n";
}

void PrintIssues(const TVector<FValidationIssue>& Issues)
{
    for (const FValidationIssue& Issue : Issues)
    {
        const char* Severity = "info";
        if (Issue.Severity == EValidationSeverity::Warning)
        {
            Severity = "warning";
        }
        else if (Issue.Severity == EValidationSeverity::Error)
        {
            Severity = "error";
        }

        std::cout << Severity << ": " << Issue.FieldPath << " " << Issue.Code << " " << Issue.Message << "\n";
    }
}

void HandleSignal(int)
{
    GKeepRunning.store(false);
}
}

int main(int argc, char** argv)
{
    if (argc <= 1)
    {
        PrintUsage();
        return 1;
    }

    MString NewMonsterName;
    MString OpenPath;
    MString CategoryPath = "Combat/Monsters";
    bool bSave = false;
    bool bValidate = false;
    bool bExport = false;
    bool bPublish = false;
    bool bRoundTrip = false;
    bool bServe = false;
    uint16 Port = 18081;

    for (int Index = 1; Index < argc; ++Index)
    {
        const MString Arg = argv[Index];
        if (Arg == "--new-monster" && Index + 1 < argc)
        {
            NewMonsterName = argv[++Index];
        }
        else if (Arg == "--category" && Index + 1 < argc)
        {
            CategoryPath = argv[++Index];
        }
        else if (Arg == "--open" && Index + 1 < argc)
        {
            OpenPath = argv[++Index];
        }
        else if (Arg == "--save")
        {
            bSave = true;
        }
        else if (Arg == "--validate")
        {
            bValidate = true;
        }
        else if (Arg == "--export")
        {
            bExport = true;
        }
        else if (Arg == "--publish")
        {
            bPublish = true;
        }
        else if (Arg == "--roundtrip")
        {
            bRoundTrip = true;
        }
        else if (Arg == "--serve")
        {
            bServe = true;
        }
        else if (Arg == "--port" && Index + 1 < argc)
        {
            Port = static_cast<uint16>(std::stoi(argv[++Index]));
        }
        else
        {
            std::cerr << "unknown_arg: " << Arg << "\n";
            PrintUsage();
            return 2;
        }
    }

    if (!NewMonsterName.empty() && !OpenPath.empty())
    {
        std::cerr << "new_and_open_conflict\n";
        return 3;
    }
    if (!bServe && NewMonsterName.empty() && OpenPath.empty())
    {
        std::cerr << "document_target_required\n";
        return 4;
    }

    MObjectEditorServiceApp App;
    MString Error;
    if (!App.Initialize(Error))
    {
        std::cerr << Error << "\n";
        return 5;
    }

    if (!NewMonsterName.empty())
    {
        FEditorAssetIdentity AssetId;
        AssetId.AssetName = NewMonsterName;
        AssetId.CategoryPath = CategoryPath;
        AssetId.SourcePath = MEditorPaths::BuildMonsterConfigPaths(AssetId).SourcePath;
        if (!App.CreateNewMonsterConfig(AssetId, Error))
        {
            std::cerr << Error << "\n";
            return 6;
        }
        std::cout << "created: " << AssetId.SourcePath << "\n";
    }
    else if (!OpenPath.empty() && !App.OpenMonsterConfig(OpenPath, Error))
    {
        std::cerr << Error << "\n";
        return 7;
    }

    if (bSave)
    {
        if (!App.SaveCurrentDocument(Error))
        {
            std::cerr << Error << "\n";
            return 8;
        }
        std::cout << "saved: " << App.GetCurrentDocument()->GetIdentity().SourcePath << "\n";
    }

    if (bValidate)
    {
        const TVector<FValidationIssue> Issues = App.ValidateCurrentDocument();
        PrintIssues(Issues);
    }

    if (bExport || bPublish)
    {
        FAssetExportOptions Options;
        Options.bExportJson = true;
        Options.bExportMob = true;
        Options.bExportRoundTripJson = bRoundTrip;
        Options.bPublishMob = bPublish;

        const FAssetExportResult Result = App.ExportCurrentDocument(Options);
        if (!Result.bSuccess)
        {
            std::cerr << Result.Error << "\n";
            PrintIssues(Result.Issues);
            return 9;
        }

        std::cout << "export_json: " << Result.JsonPath << "\n";
        std::cout << "export_mob: " << Result.MobPath << "\n";
        if (!Result.RoundTripPath.empty())
        {
            std::cout << "roundtrip_json: " << Result.RoundTripPath << "\n";
        }
        if (!Result.PublishPath.empty())
        {
            std::cout << "publish_mob: " << Result.PublishPath << "\n";
        }
    }

    if (bServe)
    {
        MEditorApiController Controller(App);
        MEditorHttpServer Server(
            Port,
            [&Controller](const FEditorHttpRequest& Request)
            {
                return Controller.HandleRequest(Request);
            });

        if (!Server.Start())
        {
            std::cerr << "http_server_start_failed\n";
            return 10;
        }

        std::signal(SIGINT, HandleSignal);
#if defined(SIGTERM)
        std::signal(SIGTERM, HandleSignal);
#endif

        std::cout << "serving: http://127.0.0.1:" << static_cast<unsigned>(Port) << "/\n";
        while (GKeepRunning.load())
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(200));
        }

        Server.Stop();
    }

    return 0;
}
