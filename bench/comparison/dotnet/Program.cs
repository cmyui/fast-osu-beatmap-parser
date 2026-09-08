using System.Diagnostics;
using System.Globalization;
using System.Text.Json;
using System.Text;
using osu.Framework.Logging;
using osu.Game.Beatmaps;
using osu.Game.IO;
using osu.Game.Rulesets;
using Decoder = osu.Game.Beatmaps.Formats.Decoder;

CultureInfo.CurrentCulture = CultureInfo.InvariantCulture;
string name = args[0];
if (name == "osu-lazer")
{
    Logger.Enabled = false;
    _ = new osu.Game.Rulesets.Osu.OsuRuleset();
    _ = new osu.Game.Rulesets.Taiko.TaikoRuleset();
    _ = new osu.Game.Rulesets.Catch.CatchRuleset();
    _ = new osu.Game.Rulesets.Mania.ManiaRuleset();
    Decoder.RegisterDependencies(new AssemblyRulesetStore());
}

int ParseAndCount(byte[] data, string path, string workload)
{
    using Stream stream = workload == "file"
        ? File.OpenRead(path) : new MemoryStream(data, writable: false);
    if (name == "osuparsers")
    {
        // The Stream overload splits on Environment.NewLine, leaving CRs on
        // Linux. Use the public line API; decoding/line splitting stays timed.
        using var text = new StreamReader(stream, Encoding.UTF8, detectEncodingFromByteOrderMarks: true);
        var map = OsuParsers.Decoders.BeatmapDecoder.Decode(ReadLines(text));
        int count = map.HitObjects.Count;
        GC.KeepAlive(map);
        return count;
    }
    if (name == "coosu")
    {
        var map = Coosu.Beatmap.OsuFile.ReadFromStream(stream);
        int count = map.HitObjects!.HitObjectList.Count;
        GC.KeepAlive(map);
        return count;
    }
    if (name != "osu-lazer") throw new ArgumentException(name);
    using var reader = new LineBufferedReader(stream);
    var decoder = Decoder.GetDecoder<Beatmap>(reader);
    var beatmap = decoder.Decode(reader);
    int objects = beatmap.HitObjects.Count;
    GC.KeepAlive(beatmap);
    return objects;
}

IEnumerable<string> ReadLines(StreamReader reader)
{
    string? line;
    while ((line = reader.ReadLine()) != null) yield return line;
}

string? line;
while ((line = Console.ReadLine()) != null)
{
    try
    {
        using var request = JsonDocument.Parse(line);
        var root = request.RootElement;
        string path = root.GetProperty("path").GetString()!;
        string workload = root.GetProperty("workload").GetString()!;
        byte[] data = File.ReadAllBytes(path);
        int count = 0;
        var ns = new List<double>();
        for (int i = 0; i < root.GetProperty("reps").GetInt32(); ++i)
        {
            long start = Stopwatch.GetTimestamp();
            count = ParseAndCount(data, path, workload);
            ns.Add(Stopwatch.GetElapsedTime(start).TotalNanoseconds);
        }
        Console.WriteLine(JsonSerializer.Serialize(new {count, ns}));
    }
    catch (Exception error)
    {
        Console.WriteLine(JsonSerializer.Serialize(new {
            error = error.GetType().Name, detail = error.Message[..Math.Min(240, error.Message.Length)]
        }));
    }
}
