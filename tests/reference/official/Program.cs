// Executes the official decoder without replacing its parsing or error policy.
// Requests are JSON lines with a local "path"; optional "values" includes raw fields.
using System.Globalization;
using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.Text.Json.Serialization;
using osu.Framework.Logging;
using osu.Game.Beatmaps;
using osu.Game.Beatmaps.Formats;
using osu.Game.IO;
using osu.Game.Rulesets;
using osu.Game.Rulesets.Osu;
using osu.Game.Rulesets.Taiko;
using osu.Game.Rulesets.Catch;
using osu.Game.Rulesets.Mania;
using Decoder = osu.Game.Beatmaps.Formats.Decoder;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
Logger.Enabled = false;
// Load all legacy ruleset assemblies before the store enumerates them.
_ = new OsuRuleset(); _ = new TaikoRuleset(); _ = new CatchRuleset(); _ = new ManiaRuleset();
Decoder.RegisterDependencies(new AssemblyRulesetStore());
AuditDecoder.RegisterAudit();
var json = new JsonSerializerOptions { NumberHandling = JsonNumberHandling.AllowNamedFloatingPointLiterals };
string? request;
while ((request = Console.ReadLine()) != null)
{
    try
    {
        using var input = JsonDocument.Parse(request);
        using var stream = File.OpenRead(input.RootElement.GetProperty("path").GetString()!);
        using var reader = new LineBufferedReader(stream);
        var decoder = (AuditDecoder)Decoder.GetDecoder<Beatmap>(reader);
        decoder.CaptureValues = input.RootElement.TryGetProperty("values", out var capture) && capture.GetBoolean();
        var map = decoder.Decode(reader);
        if (decoder.CaptureValues)
            decoder.FinishValues(map);
        Console.WriteLine(JsonSerializer.Serialize(new {
            ok = true,
            format = map.BeatmapVersion,
            mode = map.BeatmapInfo.Ruleset.OnlineID,
            ar = map.Difficulty.ApproachRate, cs = map.Difficulty.CircleSize,
            od = map.Difficulty.OverallDifficulty,
            objects = map.HitObjects.Count,
            bookmarks = map.Bookmarks,
            accepted = decoder.Accepted,
            rejected = decoder.Rejected,
            values = decoder.CaptureValues ? decoder.Values : null,
            projection_errors = decoder.ProjectionErrors,
        }, json));
    }
    catch (Exception e)
    {
        Console.WriteLine(JsonSerializer.Serialize(new { ok = false, error = e.GetType().Name }));
    }
}

sealed class AuditDecoder(int version) : LegacyBeatmapDecoder(version)
{
    public readonly Dictionary<string, int> Accepted = new();
    public readonly List<object> Rejected = new();
    public bool CaptureValues;
    public readonly RawFields Values = new();
    public readonly List<object> ProjectionErrors = new();
    public void FinishValues(Beatmap map)
    {
        try { Values.Finish(map); }
        catch (Exception e) { ProjectionErrors.Add(new { section = "FinishedMap", error = e.GetType().Name }); }
    }
    public static void RegisterAudit()
    {
        AddDecoder<Beatmap>("osu file format v", line => new AuditDecoder(Parsing.ParseInt(line.Split('v').Last())));
        SetFallbackDecoder<Beatmap>(() => new AuditDecoder(LATEST_VERSION));
    }
    protected override void ParseLine(Beatmap output, Section section, string line, bool primary)
    {
        try
        {
            base.ParseLine(output, section, line, primary);
            var name = section.ToString();
            Accepted[name] = Accepted.GetValueOrDefault(name) + 1;
        }
        catch (Exception e)
        {
            Rejected.Add(new {
                section = section.ToString(), error = e.GetType().Name,
                sha256 = Convert.ToHexString(SHA256.HashData(Encoding.UTF8.GetBytes(line))).ToLowerInvariant(),
            });
            throw; // The unmodified outer decoder decides whether to continue.
        }
        if (CaptureValues)
        {
            try { Values.Capture(output, section.ToString(), line); }
            catch (Exception e)
            {
                // An audit bug must never become an official rejection.
                ProjectionErrors.Add(new { section = section.ToString(), error = e.GetType().Name });
            }
        }
    }
}
