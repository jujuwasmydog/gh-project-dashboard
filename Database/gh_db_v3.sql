CREATE TABLE greenhouse_samples (
  ts INTEGER PRIMARY KEY,        -- epoch ms (or seconds; pick one and stick to it)
  t1   REAL,
  t2   REAL,
  tavg REAL,
  h1   REAL,
  h2   REAL,
  havg REAL,
  soil REAL,
  lux  REAL
, louvre_pct REAL, louvre_state TEXT, fan_pct REAL);
CREATE INDEX idx_greenhouse_samples_ts
ON greenhouse_samples(ts);
