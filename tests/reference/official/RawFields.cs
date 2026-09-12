// Test-only raw projection. The unmodified decoder decides acceptance first.
// Numeric conversion uses its public Parsing helpers, never FOSU's implementation.
// Raw values discarded by gameplay processing are recovered from accepted lines;
// directly comparable object values are also taken from the decoded objects.
using System.Globalization;
using osu.Game.Beatmaps;
using osu.Game.Beatmaps.Formats;
using osu.Game.Rulesets.Objects;
using osu.Game.Rulesets.Objects.Types;

sealed class RawFields
{
    readonly record struct Point(double x, double y);
    // The pinned official enum is internal; use it without duplicating its values.
    static readonly Type sampleBankType = typeof(LegacyBeatmapDecoder).Assembly.GetType("osu.Game.Beatmaps.Legacy.LegacySampleBank", throwOnError: true)!;
    public Dictionary<string, object?> fields { get; } = new();
    public List<object> hit_objects { get; } = new();
    public List<object> timing_points { get; } = new();
    public List<object> breaks { get; } = new();
    public List<uint> combo_colours { get; } = new();
    public List<string> policy_rejections { get; } = new();
    readonly Dictionary<HitObject, Dictionary<string, object?>> objects = new();

    public void Finish(Beatmap map)
    {
        fields["hp"] = (double)map.Difficulty.DrainRate;
        fields["cs"] = (double)map.Difficulty.CircleSize;
        fields["od"] = (double)map.Difficulty.OverallDifficulty;
        fields["ar"] = (double)map.Difficulty.ApproachRate;
        fields["slider_multiplier"] = map.Difficulty.SliderMultiplier;
        fields["slider_tick_rate"] = map.Difficulty.SliderTickRate;
        fields["stack_leniency"] = (double)map.StackLeniency;
        fields["distance_spacing"] = map.DistanceSpacing;
        fields["beat_divisor"] = map.BeatmapInfo.BeatDivisor;
        fields["grid_size"] = map.GridSize;
        fields["timeline_zoom"] = map.TimelineZoom;
        fields["preview_time"] = map.Metadata.PreviewTime == -1 ? null : map.Metadata.PreviewTime;
        fields["bookmark_list"] = map.Bookmarks;
        fields["velocity_presets"] = map.SliderVelocityPresets;
        hit_objects.Clear();
        foreach (var decoded in map.HitObjects)
        {
            if (!objects.TryGetValue(decoded, out var record)) continue;
            var combo = (IHasCombo)decoded;
            var position = ((IHasPosition)decoded).Position;
            record["x"] = (double)position.X;
            record["y"] = (double)position.Y;
            record["time"] = decoded.StartTime;
            record["new_combo"] = combo.NewCombo;
            record["combo_skip"] = combo.ComboOffset;
            record["end_time"] = decoded is IHasDuration duration ? duration.EndTime : decoded.StartTime;
            if (decoded is IHasPath slider)
            {
                record["path"] = new { points = slider.Path.CalculatedPath.Select(p => new { x = (double)p.X, y = (double)p.Y }).ToArray() };
                record["path_distance"] = slider.Path.Distance;
                var repeated = (IHasPathWithRepeats)decoded;
                var velocity = (double)decoded.GetType().GetField("Velocity")!.GetValue(decoded)!;
                var beatLength = map.ControlPointInfo.TimingPointAt(decoded.StartTime).BeatLength;
                var speed = ((IHasSliderVelocity)decoded).SliderVelocityMultiplier;
                var tickDistance = ((IHasGenerateTicks)decoded).GenerateTicks
                    ? velocity * beatLength / map.Difficulty.SliderTickRate * (map.BeatmapVersion < 8 ? 1 / speed : 1)
                    : double.PositiveInfinity;
                record["events"] = SliderEventGenerator.Generate(decoded.StartTime,
                    ((IHasDuration)decoded).Duration / repeated.SpanCount(), velocity,
                    tickDistance, slider.Path.Distance, repeated.SpanCount())
                    .Select(e => {
                        // Keep FOSU's public result finite for a degenerate zero-duration
                        // slider; the official division produces NaN for this progress.
                        var progress = e.Type == SliderEventType.LegacyLastTick && double.IsNaN(e.PathProgress)
                            ? repeated.SpanCount() % 2
                            : e.PathProgress;
                        var p = slider.Path.PositionAt(progress);
                        return new {
                            type = e.Type switch {
                                SliderEventType.Head => 0,
                                SliderEventType.Tick => 1,
                                SliderEventType.Repeat => 2,
                                SliderEventType.LegacyLastTick => 3,
                                _ => 4,
                            },
                            time = e.Time, span_index = e.SpanIndex, span_start_time = e.SpanStartTime,
                            path_progress = progress, position = new { x = (double)p.X, y = (double)p.Y },
                        };
                    }).ToArray();
                record["path_samples"] = new[] { 0.0, 0.1, 0.5, 0.9, 1.0 }.Select(progress => {
                    var p = slider.Path.PositionAt(progress);
                    return new { x = (double)p.X, y = (double)p.Y };
                }).ToArray();
            }
            hit_objects.Add(record);
        }
        if (map.BeatmapInfo.Ruleset.OnlineID == 0)
        {
            var converted = new osu.Game.Rulesets.Osu.Beatmaps.OsuBeatmapConverter(map, new osu.Game.Rulesets.Osu.OsuRuleset()).Convert();
            foreach (var obj in converted.HitObjects)
                obj.ApplyDefaults(converted.ControlPointInfo, converted.Difficulty);
            new osu.Game.Rulesets.Osu.Beatmaps.OsuBeatmapProcessor(converted).PostProcess();
            for (int i = 0; i < map.HitObjects.Count; ++i)
            {
                if (!objects.TryGetValue(map.HitObjects[i], out var record)) continue;
                var obj = (osu.Game.Rulesets.Osu.Objects.OsuHitObject)converted.HitObjects[i];
                record["stacking"] = new { stack_height = obj.StackHeight,
                    stack_offset = new { x = (double)obj.StackOffset.X, y = (double)obj.StackOffset.Y } };
                record["x"] = (double)obj.StackedPosition.X;
                record["y"] = (double)obj.StackedPosition.Y;
                if (record.TryGetValue("control_points", out var points))
                    record["control_points"] = ((Point[])points!).Select(p =>
                        new Point((float)p.x + obj.StackOffset.X, (float)p.y + obj.StackOffset.Y)).ToArray();
            }
        }
    }

    static double Number(string text) => Parsing.ParseDouble(text);
    static int Integer(string text) => Parsing.ParseInt(text);
    static int Coordinate(string text) => (int)Parsing.ParseFloat(text, Parsing.MAX_COORDINATE_VALUE);
    static double Coordinate(string text, bool preserveFraction) => preserveFraction
        ? Parsing.ParseFloat(text, Parsing.MAX_COORDINATE_VALUE)
        : Coordinate(text);
    static string At(string[] fields, int index, string fallback = "") => index < fields.Length ? fields[index] : fallback;

    static (string type, int degree) Curve(string text)
    {
        int degree = text[0] == 'B' && text.Length > 1 && int.TryParse(text.AsSpan(1), out int value) && value > 0
            ? value
            : 0;
        return (text[0].ToString(), degree);
    }

    static object[] CurveSegments(string[] path, Point head, bool preserveFraction)
    {
        var curve = Curve(path[0]);
        var points = new List<Point> { head };
        var segments = new List<object>();
        (string type, int degree)? pending = null;
        bool segmented = curve.degree != 0;
        foreach (string token in path.Skip(1))
        {
            if (char.IsLetter(token[0]))
            {
                pending = Curve(token);
                segmented = true;
                continue;
            }
            var xy = token.Split(':');
            var point = new Point(Coordinate(xy[0], preserveFraction), Coordinate(xy[1], preserveFraction));
            points.Add(point);
            if (pending == null) continue;
            segments.Add(new { type = curve.type, degree = curve.degree, control_points = points.ToArray() });
            curve = pending.Value;
            points = new List<Point> { point };
            pending = null;
        }
        if (segmented)
            segments.Add(new { type = curve.type, degree = curve.degree, control_points = points.ToArray() });
        return segments.ToArray();
    }

    public void Capture(Beatmap map, string section, string line)
    {
        switch (section)
        {
            case "HitObjects": CaptureObject(map, line); return;
            case "TimingPoints": CaptureTiming(line, map.BeatmapVersion < 5 ? 24 : 0); return;
            case "Events": CaptureEvent(map, line); return;
            case "Colours":
                var colour = line.Split(':', 2, StringSplitOptions.TrimEntries);
                if (colour[0].StartsWith("Combo", StringComparison.Ordinal)
                    && int.TryParse(colour[0][5..], out var index) && index is >= 1 and <= 8)
                {
                    var rgb = colour[1].Split(',');
                    combo_colours.Add((uint)(byte.Parse(rgb[0]) << 16 | byte.Parse(rgb[1]) << 8 | byte.Parse(rgb[2])));
                }
                return;
        }
        var pair = line.Split(':', 2, StringSplitOptions.TrimEntries);
        var value = At(pair, 1);
        switch (section + "." + pair[0])
        {
            case "General.AudioFilename": fields["audio_filename"] = value; break;
            case "General.AudioLeadIn": fields["audio_lead_in"] = map.AudioLeadIn; break;
            case "General.PreviewTime": fields["preview_time"] = Integer(value) == -1 ? null : Integer(value); break;
            case "General.Countdown": fields["countdown"] = (int)map.Countdown; break;
            case "General.CountdownOffset": fields["countdown_offset"] = map.CountdownOffset; break;
            case "General.SampleSet":
                var sample = Convert.ToInt32(Enum.Parse(sampleBankType, value));
                if (sample is < 0 or > 3 || value.Contains(',')) policy_rejections.Add("General.SampleSet");
                else fields["sample_set"] = sample;
                break;
            case "General.StackLeniency": fields["stack_leniency"] = double.Parse(value, CultureInfo.InvariantCulture); break;
            case "General.Mode": fields["mode"] = map.BeatmapInfo.Ruleset.OnlineID; break;
            case "General.LetterboxInBreaks": fields["letterbox_in_breaks"] = map.LetterboxInBreaks; break;
            case "General.WidescreenStoryboard": fields["widescreen_storyboard"] = map.WidescreenStoryboard; break;
            case "General.EpilepsyWarning": fields["epilepsy_warning"] = map.EpilepsyWarning; break;
            case "General.SpecialStyle": fields["special_style"] = map.SpecialStyle; break;
            case "General.SamplesMatchPlaybackRate": fields["samples_match_playback_rate"] = map.SamplesMatchPlaybackRate; break;
            case "General.UseSkinSprites": fields["use_skin_sprites"] = value.StartsWith('1'); break;
            case "General.OverlayPosition": fields["overlay_position"] = value; break;
            case "General.SkinPreference": fields["skin_preference"] = value; break;
            case "Editor.Bookmarks": fields["bookmarks"] = value; break;
            case "Editor.DistanceSpacing": fields["distance_spacing"] = Number(value); break;
            case "Editor.BeatDivisor": fields["beat_divisor"] = Integer(value); break;
            case "Editor.GridSize": fields["grid_size"] = map.GridSize; break;
            case "Editor.TimelineZoom": fields["timeline_zoom"] = Number(value); break;
            case "Metadata.Title": fields["title"] = map.Metadata.Title; break;
            case "Metadata.TitleUnicode": fields["title_unicode"] = map.Metadata.TitleUnicode; break;
            case "Metadata.Artist": fields["artist"] = map.Metadata.Artist; break;
            case "Metadata.ArtistUnicode": fields["artist_unicode"] = map.Metadata.ArtistUnicode; break;
            case "Metadata.Creator": fields["creator"] = map.Metadata.Author.Username; break;
            case "Metadata.Version": fields["version"] = map.BeatmapInfo.DifficultyName; break;
            case "Metadata.Source": fields["source"] = map.Metadata.Source; break;
            case "Metadata.Tags": fields["tags"] = map.Metadata.Tags; break;
            case "Metadata.BeatmapID": fields["beatmap_id"] = Integer(value) == -1 ? null : Integer(value); break;
            case "Metadata.BeatmapSetID": fields["beatmap_set_id"] = Integer(value) == -1 ? null : Integer(value); break;
            case "Difficulty.HPDrainRate": fields["hp"] = double.Parse(value, CultureInfo.InvariantCulture); break;
            case "Difficulty.CircleSize": fields["cs"] = double.Parse(value, CultureInfo.InvariantCulture); break;
            case "Difficulty.OverallDifficulty":
                fields["od"] = double.Parse(value, CultureInfo.InvariantCulture);
                if (!arSpecified) fields["ar"] = fields["od"];
                break;
            case "Difficulty.ApproachRate": fields["ar"] = double.Parse(value, CultureInfo.InvariantCulture); arSpecified = true; break;
            case "Difficulty.SliderMultiplier": fields["slider_multiplier"] = map.Difficulty.SliderMultiplier; break;
            case "Difficulty.SliderTickRate": fields["slider_tick_rate"] = map.Difficulty.SliderTickRate; break;
        }
    }
    bool arSpecified;

    void CaptureTiming(string line, int offset)
    {
        var parts = line.Split(',');
        int sample = Integer(At(parts, 3, "0"));
        if (sample is < 0 or > 3)
        {
            policy_rejections.Add("TimingPoints.SampleSet");
            return;
        }
        string meterText = At(parts, 2, "4");
        int meter = meterText.StartsWith('0') && !int.TryParse(meterText, out _) ? 4 : Integer(meterText);
        timing_points.Add(new {
            time = Number(parts[0]) + offset, beat_length = Parsing.ParseDouble(parts[1], allowNaN: true),
            meter, sample_set = sample, sample_index = Integer(At(parts, 4, "0")),
            volume = Integer(At(parts, 5, "100")), uninherited = At(parts, 6, "1").StartsWith('1'),
            effects = unchecked((uint)Integer(At(parts, 7, "0"))),
        });
    }

    void CaptureObject(Beatmap map, string line)
    {
        var parts = line.Split(',');
        uint type = unchecked((uint)Integer(parts[3]));
        string kind = (type & 1) != 0 ? "circle" : (type & 2) != 0 ? "slider" : (type & 8) != 0 ? "spinner" : "hold";
        var decoded = map.HitObjects[^1];
        var position = ((IHasPosition)decoded).Position;
        var result = new Dictionary<string, object?> {
            ["kind"] = kind, ["time"] = decoded.StartTime,
            // The official decoder relocates spinners to the playfield centre.
            ["x"] = kind == "spinner" ? Coordinate(parts[0]) : (int)position.X,
            ["y"] = kind == "spinner" ? Coordinate(parts[1]) : (int)position.Y,
            ["type"] = type, ["hitsound"] = unchecked((uint)Integer(parts[4])),
        };
        string sample = "";
        switch (kind)
        {
            case "circle": sample = At(parts, 5); break;
            case "spinner": sample = At(parts, 6); break;
            case "hold":
                var tail = At(parts, 5);
                var colon = tail.IndexOf(':');
                sample = colon < 0 ? "" : tail[(colon + 1)..];
                break;
            case "slider":
                var path = parts[5].Split('|');
                if (path[0].Length == 0 || !"BCLP".Contains(path[0][0]))
                {
                    policy_rejections.Add("HitObjects.CurveType");
                    return;
                }
                bool preserveFraction = map.BeatmapVersion >= LegacyBeatmapEncoder.FIRST_LAZER_VERSION;
                var head = new Point(Coordinate(parts[0], preserveFraction), Coordinate(parts[1], preserveFraction));
                result["curve_type"] = path[0][0].ToString();
                result["slides"] = Math.Max(1, Integer(parts[6]));
                result["length"] = parts.Length > 7 ? Math.Max(0, Parsing.ParseDouble(parts[7], Parsing.MAX_COORDINATE_VALUE)) : 0.0;
                result["edge_sounds"] = At(parts, 8);
                result["edge_sets"] = At(parts, 9);
                result["control_points"] = new[] { head }.Concat(path.Skip(1).Where(point => !char.IsLetter(point[0])).Select(point => {
                    var xy = point.Split(':');
                    return new Point(Coordinate(xy[0], preserveFraction), Coordinate(xy[1], preserveFraction));
                })).ToArray();
                result["curve_segments"] = CurveSegments(path, head, preserveFraction);
                sample = At(parts, 10);
                break;
        }
        result["hit_sample"] = sample;
        objects.Add(decoded, result);
    }

    void CaptureEvent(Beatmap map, string line)
    {
        var parts = line.Split(',');
        switch (parts[0])
        {
            case "0": case "Background": fields["background"] = parts[2].Trim().Trim('"'); break;
            case "1": case "Video": fields["video"] = parts[2].Trim().Trim('"'); break;
            case "2": case "Break": breaks.Add(new { start = map.Breaks[^1].StartTime, end = map.Breaks[^1].EndTime }); break;
        }
    }
}
