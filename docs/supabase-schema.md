# Supabase schema (race results)

The board writes every race to `data/UFLI_races.csv` first (the backup), then uploads the
same rows to Supabase from `firmware/src/database/SupabaseSync.cpp`.

The real migration lives in the website repo (Ruff-Labs org). This file is a copy so firmware
work doesn't need that repo. **If you change one, change the other.**

## Table

```sql
create table race_results (
  id          bigint generated always as identity primary key,
  board_id    text not null,
  heat        int  not null,
  race        int  not null,
  lane        text not null,                -- "Left" / "Right"
  start_time  timestamptz,
  splits_ms   double precision[],           -- FirstPass, Dog1..Dog9
  race_time   text,                         -- ms, or "FS" / "DNF" (match Database.cpp TODO 4)
  modified_at timestamptz,
  operator    text,
  unique (board_id, race, lane)
);

alter table race_results enable row level security;
create policy "board insert" on race_results for insert to anon with check (true);
create policy "public read"  on race_results for select to anon using (true);
```

## How the board uploads

- `POST {SUPABASE_URL}/rest/v1/race_results?on_conflict=board_id,race,lane`
- Headers: `apikey`, `Authorization: Bearer <key>`, `Content-Type: application/json`,
  `Prefer: resolution=ignore-duplicates,return=minimal`
- Body: JSON array, one object per CSV row, keys = column names above.
- Resending a race is safe: the unique key + `ignore-duplicates` skips rows that already exist
  (`ON CONFLICT DO NOTHING`, so the anon role only needs INSERT).
- Progress is kept in `data/last_synced_race`. On boot the board re-sends every CSV row after it.

## Board config

`/etc/ruff/firmware.env` (created empty by `scripts/setup.sh`, loaded by `firmware.service`):

```
SUPABASE_URL=https://<project>.supabase.co
SUPABASE_KEY=<anon key>     # never the service_role key
BOARD_ID=<name for this board>
```

Leave it empty and the firmware runs CSV-only.
