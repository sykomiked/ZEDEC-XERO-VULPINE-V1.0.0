// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System.Text.Json;
using Xunit;

namespace Zedec.Zxv.Tests
{
    public class HexagramTests
    {
        [Fact]
        public void ConformanceTagsNamesActions()
        {
            JsonElement hex = Vectors.Load().GetProperty("hex");
            foreach (JsonElement e in hex.GetProperty("tags").EnumerateArray())
            {
                Hexagram h = Hexagram.FromTag(e.GetProperty("tag").GetInt32());
                Assert.Equal(e.GetProperty("name").GetString(), h.Name);
                Assert.Equal(e.GetProperty("base").GetInt32(), (int)h.Base);
                Assert.Equal(e.GetProperty("polarity").GetInt32(), (int)h.Polarity);
                Assert.Equal(e.GetProperty("phase").GetInt32(), (int)h.Phase);
                Assert.Equal(e.GetProperty("action").GetInt32(), (int)h.Action);
                Assert.Equal(h, Hexagram.Of(h.Base, h.Polarity, h.Phase));
            }
        }

        [Fact]
        public void ConformanceNotAndOr()
        {
            JsonElement hex = Vectors.Load().GetProperty("hex");
            JsonElement not = hex.GetProperty("not"), and = hex.GetProperty("and"), or = hex.GetProperty("or");
            for (int a = 0; a < 64; a++)
            {
                Hexagram ha = Hexagram.FromTag(a);
                Assert.Equal(not[a].GetInt32(), (!ha).Tag);
                for (int b = 0; b < 64; b++)
                {
                    Hexagram hb = Hexagram.FromTag(b);
                    Assert.Equal(and[a][b].GetInt32(), (ha & hb).Tag);
                    Assert.Equal(or[a][b].GetInt32(), (ha | hb).Tag);
                }
            }
        }

        [Fact]
        public void ConformanceStepAndSet()
        {
            JsonElement hex = Vectors.Load().GetProperty("hex");
            JsonElement step = hex.GetProperty("step"), set = hex.GetProperty("set");
            for (int t = 0; t < 64; t++)
            {
                Hexagram h = Hexagram.FromTag(t);
                for (int p = 0; p < 4; p++)
                {
                    int want = step[t][p].GetInt32();
                    if (want < 0)
                        Assert.Equal(ZxvStatus.State, Assert.Throws<ZxvException>(() => h.Step((HexPhase)p)).Status);
                    else
                        Assert.Equal(want, h.Step((HexPhase)p).Tag);
                }
                for (int bp = 0; bp < 16; bp++)
                {
                    int want = set[t][bp].GetInt32();
                    if (want < 0)
                        Assert.Equal(ZxvStatus.State,
                            Assert.Throws<ZxvException>(() => h.WithState((HexBase)(bp >> 2), (HexPolarity)(bp & 3))).Status);
                    else
                        Assert.Equal(want, h.WithState((HexBase)(bp >> 2), (HexPolarity)(bp & 3)).Tag);
                }
            }
        }

        [Fact]
        public void TruthBridgeIsExplicitAndNotACast()
        {
            JsonElement lpToBase = Vectors.Load().GetProperty("hex").GetProperty("lp_to_base");
            for (int lp = 0; lp < 4; lp++)
            {
                Hexagram h = Hexagram.FromTruth((Truth)lp, HexPolarity.Neutral, HexPhase.Potential);
                Assert.Equal(lpToBase[lp].GetInt32(), (int)h.Base);
                Assert.Equal((Truth)lp, h.ToTruth());
            }
            // TRUE is base 3 but Truth 1.
            Assert.Equal(HexBase.True, Hexagram.FromTruth(Truth.True, HexPolarity.Neutral, HexPhase.Potential).Base);
            Assert.Equal(ZxvStatus.Arg, Assert.Throws<ZxvException>(() => Hexagram.FromTruth((Truth)4, 0, 0)).Status);
        }

        [Fact]
        public void ForbiddenIsAbsorbing()
        {
            Hexagram h = Hexagram.Of(HexBase.True, HexPolarity.Negentropic, HexPhase.Potential);
            Assert.Equal(HexAction.Proceed, h.Action);
            Hexagram f = h.Forbid();
            Assert.True(f.IsForbidden);
            Assert.Equal(HexAction.Rollback, f.Action);
            Assert.Equal(ZxvStatus.State,
                Assert.Throws<ZxvException>(() => f.WithState(HexBase.True, HexPolarity.Neutral)).Status);
            Assert.True((f & h).IsForbidden);
            Assert.True((f | h).IsForbidden);
            Assert.True(f.Advance().IsForbidden);
            Assert.Equal(ZxvStatus.Arg, Assert.Throws<ZxvException>(() => Hexagram.FromTag(64)).Status);
        }
    }
}
