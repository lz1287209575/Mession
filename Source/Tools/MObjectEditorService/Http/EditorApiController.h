#pragma once

#include "Tools/MObjectEditorService/App/MObjectEditorServiceApp.h"
#include "Tools/MObjectEditorService/Http/HttpServer.h"

class MEditorApiController
{
public:
    explicit MEditorApiController(MObjectEditorServiceApp& InApp)
        : App(InApp)
    {
    }

    FEditorHttpResponse HandleRequest(const FEditorHttpRequest& Request);

private:
    FEditorHttpResponse HandleStatus() const;
    FEditorHttpResponse HandleStaticFile(const FEditorHttpRequest& Request) const;
    FEditorHttpResponse HandleListMonsterConfigs() const;
    FEditorHttpResponse HandleMonsterConfigTable() const;
    FEditorHttpResponse HandleOpenMonsterConfig(const FEditorHttpRequest& Request);
    FEditorHttpResponse HandleSaveMonsterConfig(const FEditorHttpRequest& Request);
    FEditorHttpResponse HandleBatchSaveMonsterConfigs(const FEditorHttpRequest& Request);
    FEditorHttpResponse HandleDeleteMonsterConfigs(const FEditorHttpRequest& Request);
    FEditorHttpResponse HandleValidateMonsterConfig(const FEditorHttpRequest& Request) const;
    FEditorHttpResponse HandleExportMonsterConfig(const FEditorHttpRequest& Request) const;

private:
    MObjectEditorServiceApp& App;
};
