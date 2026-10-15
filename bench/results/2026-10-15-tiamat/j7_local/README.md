# J7 rehearsal on the cloud machine (2026-10-15, 4 cores, no adapters yet)

The three disc GGUFs this machine has (Spinewyrm, Codelindworm, Vouchwyvern),
laid out as the server's models folder, assembled by
`tools/tiamat/make_swarm_config.py` and asked `tools/tiamat/bench/organ_questions.txt`
by a 12-slot `zxv-host`. `assembly.json` lists the other 26 disc cells and
why each is out here (no S3 report or GGUF on this machine, or no engine path).

- Routing: code questions go to Codelindworm (smith), the argument question
  to Vouchwyvern (arbiter), the rest to the lead, Spinewyrm.
- Witness: with lineage by developer, every Qwen answer is witnessed by
  Vouchwyvern (Hugging Face) and Vouchwyvern's by Spinewyrm.
  `organs_lineage_by_trunk.jsonl` is the first try, with lineage by trunk:
  Codelindworm then witnessed Spinewyrm, two Qwen2.5 models sharing one
  pretraining. That is why the builder now takes lineage by developer.
- "What is 17 times 23?" is TRUE by the reckoner (391).
- Memory: the builder estimated 4,330,438,432 resident bytes (each GGUF
  whole, plus each cell's full Q16 KV cache at 2048 tokens; Vouchwyvern's
  alone is 805 MB, as SmolLM2-1.7B keeps a KV head per query head) against
  a budget of 8,085,194,752 (half the free memory). One question through
  the same three cells peaked at 2,269,396,992 bytes resident (getrusage):
  the estimate is an upper bound, as it should be for a hard budget.
