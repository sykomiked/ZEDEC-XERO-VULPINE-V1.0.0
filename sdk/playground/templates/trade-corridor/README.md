# Settle a trade corridor treaty

Three trading partners owe each other money. Instead of three transfers,
the treaty nets them to two and settles once, with the 0.08889% assurance
fee, on a scratch engine. Every number is a whole number of cents.

```text
TREATY CORRIDOR-7 USD SETL
PARTY ACME 5000000 4000 1/3     deposit, claim back, weight
OWE ACME BETA 1200000           who owes whom before netting
END
```

The treaty is checked before anything runs: weights must add up to
exactly 1, nobody may claim more than they deposited, and amounts must
fit 18 digits. A broken rule is refused with the line and column.

## Draft it with the local AI

In the Playground tab, **Draft with AI** asks the local swarm to write a
treaty from a plain description ("Acme owes Beta 12,000 dollars..."). The
draft is untrusted: it is parsed and checked exactly like text you typed,
and nothing runs until you press Run.

## Run it

```sh
zxv new trade-corridor
cd trade-corridor
zxv run
```

Try making a claim larger than its deposit, or the weights add up to
more than 1, to see the refusal.

## Limits

A treaty is a batch description, not a contract in law. Signatures,
consent and authority live elsewhere. The dividend each party gets is
reported, not paid.
