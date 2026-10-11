// Copyright (c) 2024-2026 Michael Laurence Curzi and 36N9 Genetics, LLC
// SPDX-License-Identifier: Apache-2.0

using System;
using Xunit;

namespace Zedec.Zxv.Tests
{
    /// <summary>
    /// A small game: rational slot p is player p's position (moves 1/3 per
    /// tick), slot P a shared tide (+ (tick mod 7)/5), hexagram slot p the
    /// player's claim. Input: [direction 0/1/255, assert bit].
    /// </summary>
    internal sealed class Game : IDisposable
    {
        internal const int InputBytes = 2;
        internal readonly ZxvLockstep Sim;
        private readonly int _players;

        internal Game(int players, bool predict, int history = 16)
        {
            _players = players;
            Sim = new ZxvLockstep(players, players + 1, players, InputBytes, history, predict);
            for (int p = 0; p < players; p++)
            {
                Sim.SetRational(p, Rational.Of(p, 2));
                Sim.SetHexagram(p, Hexagram.Of(HexBase.Neither, HexPolarity.Neutral, HexPhase.Potential));
            }
            Sim.Start();
        }

        internal ZxvStatus Step(ZxvLockstep sim, ulong tick)
        {
            for (int p = 0; p < _players; p++)
            {
                byte[] input = sim.Input(p);
                ZxvStatus st = sim.Charge(Rational.One);
                if (st != ZxvStatus.Ok) return st;
                if (input[0] == 1) st = sim.AddRational(p, Rational.Of(1, 3));
                else if (input[0] == 255) st = sim.AddRational(p, Rational.Of(-1, 3));
                if (st != ZxvStatus.Ok) return st;
                Hexagram claim = sim.GetHexagram(p);
                if ((input[1] & 2) != 0) claim = claim.Forbid();
                else if ((input[1] & 1) != 0 && !claim.IsForbidden)
                    claim = claim.WithState(HexBase.True, HexPolarity.Negentropic);
                sim.SetHexagram(p, claim.Advance());
            }
            return sim.AddRational(_players, Rational.Of((long)(tick % 7), 5));
        }

        public void Dispose() => Sim.Dispose();

        internal static byte[] InputFor(int p, ulong t)
        {
            ulong x = (t * 2654435761UL) ^ ((ulong)p * 40503UL);
            byte dir = (byte)(x % 3 == 0 ? 0 : (x % 3 == 1 ? 1 : 255));
            byte flags = (byte)((x >> 3) % 5 == 0 ? 1 : (p == 1 && t == 9 ? 2 : 0));
            return new[] { dir, flags };
        }
    }

    public class LockstepTests
    {
        [Fact]
        public void PredictingPeerConvergesWithStrictPeer()
        {
            const int P = 3;
            const ulong T = 50, Lag = 5;
            using var aGame = new Game(P, predict: true);
            using var bGame = new Game(P, predict: false);
            ZxvLockstep a = aGame.Sim, b = bGame.Sim;
            Assert.False(b.Ready);
            Assert.Equal(ZxvStatus.State, b.Advance(bGame.Step));

            int rollbacks = 0;
            for (ulong t = 1; t <= T + Lag; t++)
            {
                if (t <= T)
                {
                    for (int p = 0; p < P; p++) Assert.Equal(ZxvStatus.Ok, b.SubmitInput(p, t, Game.InputFor(p, t)));
                    Assert.Equal(ZxvStatus.Ok, a.SubmitInput(0, t, Game.InputFor(0, t)));
                }
                if (t > Lag)
                    for (int p = 1; p < P; p++)
                        Assert.Equal(ZxvStatus.Ok, a.SubmitInput(p, t - Lag, Game.InputFor(p, t - Lag)));
                if (a.RollbackPendingTick != null) rollbacks++;
                Assert.Equal(ZxvStatus.Ok, a.RollbackReplay(aGame.Step));
                if (t <= T)
                {
                    Assert.True(a.Ready);
                    Assert.Equal(ZxvStatus.Ok, a.Advance(aGame.Step));
                    Assert.Equal(ZxvStatus.Ok, b.Advance(bGame.Step));
                }
            }
            Assert.True(rollbacks > 0);
            Assert.Equal(T, a.Tick);
            Assert.Equal(T, a.ConfirmedTick);
            for (ulong t = T - 10; t <= T; t++)
            {
                byte[] ha = a.Hash(t, out bool final);
                Assert.True(final);
                Assert.Equal(b.Hash(t), ha);
                Assert.Equal(Truth.True, a.Compare(t, b.Hash(t)));
            }
            Assert.Null(a.DesyncTick);
            Assert.Equal(a.GetRational(0), b.GetRational(0));
            // tide: sum over t = 1..50 of (t mod 7)/5 = (7 * 21 + 1)/5 = 148/5
            Assert.Equal(Rational.Of(148, 5), b.GetRational(P));

            Assert.Equal(Truth.False, a.Compare(T, new byte[32]));
            Assert.Equal(T, a.DesyncTick);
            Assert.Null(a.Hash(1)); // history 16: long gone
        }

        [Fact]
        public void PredictedTicksAreNotFinal()
        {
            using var g = new Game(2, predict: true);
            for (ulong t = 1; t <= 5; t++)
            {
                g.Sim.SubmitInput(0, t, Game.InputFor(0, t));
                Assert.Equal(ZxvStatus.Ok, g.Sim.Advance(g.Step));
            }
            g.Sim.Hash(3, out bool final);
            Assert.False(final);
            Assert.Equal(Truth.Neither, g.Sim.Compare(3, new byte[32]));
            Assert.Equal(0UL, g.Sim.ConfirmedTick);
        }

        [Fact]
        public void InputWindowAndDuplicates()
        {
            using var g = new Game(1, predict: false, history: 4);
            ZxvLockstep s = g.Sim;
            byte[] x = Game.InputFor(0, 3);
            Assert.Equal(ZxvStatus.Range, s.SubmitInput(0, 5, x));
            Assert.Equal(ZxvStatus.Arg, s.SubmitInput(0, 0, x));
            Assert.Equal(ZxvStatus.Arg, s.SubmitInput(1, 1, x));
            Assert.Equal(ZxvStatus.Arg, s.SubmitInput(0, 1, new byte[1]));
            Assert.Equal(ZxvStatus.Ok, s.SubmitInput(0, 3, x));
            Assert.Equal(ZxvStatus.Ok, s.SubmitInput(0, 3, x));
            Assert.Equal(ZxvStatus.Duplicate, s.SubmitInput(0, 3, new byte[] { (byte)(x[0] ^ 1), x[1] }));
        }

        [Fact]
        public void BudgetRefusesAnExpensiveTick()
        {
            using var g = new Game(2, predict: false);
            ZxvLockstep s = g.Sim;
            s.SetTickBudget(Rational.Of(2, 1)); // one unit per player
            for (ulong t = 1; t <= 3; t++)
                for (int p = 0; p < 2; p++) s.SubmitInput(p, t, Game.InputFor(p, t));
            Assert.Equal(ZxvStatus.Ok, s.Advance(g.Step));
            Assert.Equal(ZxvStatus.Budget, s.Advance((sim, tick) =>
            {
                ZxvStatus st = g.Step(sim, tick);
                return st != ZxvStatus.Ok ? st : sim.Charge(Rational.Of(1, 1000));
            }));
            Assert.Equal(1UL, s.Tick);
            Assert.Equal(ZxvStatus.Ok, s.Advance(g.Step)); // reset each tick
        }

        [Fact]
        public void StepExceptionAbortsTheTick()
        {
            using var g = new Game(1, predict: false);
            g.Sim.SubmitInput(0, 1, Game.InputFor(0, 1));
            Rational before = g.Sim.GetRational(0);
            Assert.Throws<InvalidOperationException>(() => g.Sim.Advance((sim, tick) =>
            {
                sim.SetRational(0, 99);
                throw new InvalidOperationException("boom");
            }));
            Assert.Equal(0UL, g.Sim.Tick);
            Assert.Equal(before, g.Sim.GetRational(0));
            // setters outside a tick are refused
            Assert.Equal(ZxvStatus.State, Assert.Throws<ZxvException>(() => g.Sim.SetRational(0, 1)).Status);
        }

        [Fact]
        public void StateTransferToALateJoiner()
        {
            using var a = new Game(2, predict: false);
            for (ulong t = 1; t <= 12; t++)
            {
                for (int p = 0; p < 2; p++) a.Sim.SubmitInput(p, t, Game.InputFor(p, t));
                a.Sim.Advance(a.Step);
            }
            byte[] bytes = a.Sim.ExportState();
            using var c = new ZxvLockstep(2, 3, 2, Game.InputBytes, 8, predict: false);
            c.ImportState(bytes);
            Assert.Equal(12UL, c.Tick);
            Assert.Equal(a.Sim.Hash(12), c.Hash(12));
            bytes[0] ^= 1;
            Assert.Equal(ZxvStatus.Format, Assert.Throws<ZxvException>(() => c.ImportState(bytes)).Status);
        }

        [Fact]
        public void FixedStepClockInMicroseconds()
        {
            using var g = new Game(1, predict: false);
            g.Sim.ClockSetStep(Rational.Of(1, 60), 4);
            Assert.Equal(2, g.Sim.ClockFeedMicroseconds(33334));
            Rational alpha = g.Sim.ClockAlpha();
            Assert.True(alpha > Rational.Zero && alpha < Rational.One);
            Assert.Equal(4, g.Sim.ClockFeedMicroseconds(1000000));
        }

        [Fact]
        public void ReadmeQuickstart()
        {
            using var sim = new ZxvLockstep(players: 2, ratSlots: 2, hexSlots: 0,
                                            inputBytes: 1, history: 32, predict: false);
            sim.SetRational(0, Rational.Parse("2.5"));
            sim.Start();
            static ZxvStatus Step(ZxvLockstep s, ulong tick) =>
                s.Input(0)[0] == 1 ? s.AddRational(0, Rational.Of(1, 3)) : ZxvStatus.Ok;
            sim.SubmitInput(0, 1, new byte[] { 1 });
            sim.SubmitInput(1, 1, new byte[] { 0 });
            Assert.Equal(ZxvStatus.Ok, sim.Advance(Step));
            Assert.Equal("17/6", sim.GetRational(0).ToString());
            Assert.Equal(32, sim.Hash(1)!.Length);
        }

        [Fact]
        public void DisposeIsIdempotentAndFinal()
        {
            var g = new Game(1, predict: false);
            g.Sim.Dispose();
            g.Sim.Dispose();
            Assert.Throws<ObjectDisposedException>(() => g.Sim.Tick);
        }
    }
}
