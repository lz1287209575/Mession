using Mession.Tools.MObjectEditorAvalonia.Models;
using System.Net.Http.Json;
using System.Text.Json;

namespace Mession.Tools.MObjectEditorAvalonia.Services;

public sealed class EditorApiClient
{
    private readonly HttpClient _httpClient;
    private readonly JsonSerializerOptions _jsonOptions = new()
    {
        PropertyNameCaseInsensitive = true,
    };

    public EditorApiClient(string? baseUrl = null)
    {
        _httpClient = new HttpClient
        {
            BaseAddress = new Uri(baseUrl ?? "http://127.0.0.1:18081/"),
            Timeout = TimeSpan.FromSeconds(10),
        };
    }

    public Task<EditorStatusDto> GetStatusAsync(CancellationToken cancellationToken = default)
        => GetAsync<EditorStatusDto>("api/status", cancellationToken);

    public Task<MonsterConfigTableResponseDto> GetMonsterConfigsTableAsync(CancellationToken cancellationToken = default)
        => GetAsync<MonsterConfigTableResponseDto>("api/monster-configs/table", cancellationToken);

    public Task<BatchSaveResponseDto> SaveMonsterConfigsBatchAsync(BatchSaveRequestDto request, CancellationToken cancellationToken = default)
        => PostAsync<BatchSaveRequestDto, BatchSaveResponseDto>("api/monster-configs/batch-save", request, cancellationToken);

    public Task<DeleteResponseDto> DeleteMonsterConfigsAsync(DeleteRequestDto request, CancellationToken cancellationToken = default)
        => PostAsync<DeleteRequestDto, DeleteResponseDto>("api/monster-configs/delete", request, cancellationToken);

    public Task<ValidateResponseDto> ValidateMonsterConfigAsync(SaveDocumentDto request, CancellationToken cancellationToken = default)
        => PostAsync<SaveDocumentDto, ValidateResponseDto>("api/monster-config/validate", request, cancellationToken);

    public Task<ExportResponseDto> ExportMonsterConfigAsync(ExportRequestDto request, CancellationToken cancellationToken = default)
        => PostAsync<ExportRequestDto, ExportResponseDto>("api/monster-config/export", request, cancellationToken);

    private async Task<TResponse> GetAsync<TResponse>(string path, CancellationToken cancellationToken)
        where TResponse : ApiEnvelope, new()
    {
        using var response = await _httpClient.GetAsync(path, cancellationToken);
        return await ReadEnvelopeAsync<TResponse>(response, cancellationToken);
    }

    private async Task<TResponse> PostAsync<TRequest, TResponse>(string path, TRequest request, CancellationToken cancellationToken)
        where TResponse : ApiEnvelope, new()
    {
        using var response = await _httpClient.PostAsJsonAsync(path, request, _jsonOptions, cancellationToken);
        return await ReadEnvelopeAsync<TResponse>(response, cancellationToken);
    }

    private async Task<TResponse> ReadEnvelopeAsync<TResponse>(HttpResponseMessage response, CancellationToken cancellationToken)
        where TResponse : ApiEnvelope, new()
    {
        var payload = await response.Content.ReadFromJsonAsync<TResponse>(_jsonOptions, cancellationToken) ?? new TResponse();
        if (!response.IsSuccessStatusCode || !payload.Ok)
        {
            throw new InvalidOperationException(payload.Message ?? payload.Error ?? $"HTTP {(int)response.StatusCode}");
        }

        return payload;
    }
}
