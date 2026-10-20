-- ============================================================
-- SKEMA DATABASE LORA MONITORING UNTUK SUPABASE (POSTGRESQL)
-- Salin dan jalankan seluruh isi file ini di SQL Editor Supabase
-- ============================================================

-- 1. Tabel Nodes
CREATE TABLE IF NOT EXISTS public.nodes (
  id BIGSERIAL PRIMARY KEY,
  node_id VARCHAR(100) NOT NULL UNIQUE,
  node_name VARCHAR(150),
  latitude DOUBLE PRECISION,
  longitude DOUBLE PRECISION,
  trash_status VARCHAR(20) NOT NULL DEFAULT 'TIDAK ADA',
  servo_angle NUMERIC(6,2),
  object_distance_cm NUMERIC(10,2),
  rssi_dbm INTEGER,
  sender_time VARCHAR(30),
  last_seen_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  created_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  updated_at TIMESTAMPTZ NOT NULL DEFAULT NOW()
);

-- Index untuk nodes
CREATE INDEX IF NOT EXISTS idx_nodes_last_seen ON public.nodes (last_seen_at);

-- 2. Tabel Telemetry
CREATE TABLE IF NOT EXISTS public.telemetry (
  id BIGSERIAL PRIMARY KEY,
  node_id VARCHAR(100) NOT NULL REFERENCES public.nodes(node_id) ON UPDATE CASCADE ON DELETE CASCADE,
  sender_time VARCHAR(30),
  latitude DOUBLE PRECISION NOT NULL,
  longitude DOUBLE PRECISION NOT NULL,
  trash_status VARCHAR(20) NOT NULL,
  servo_angle NUMERIC(6,2),
  object_distance_cm NUMERIC(10,2),
  rssi_dbm INTEGER,
  received_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
  raw_payload TEXT
);

-- Index untuk telemetry
CREATE INDEX IF NOT EXISTS idx_telemetry_node_id ON public.telemetry (node_id);
CREATE INDEX IF NOT EXISTS idx_telemetry_received_at ON public.telemetry (received_at);
CREATE INDEX IF NOT EXISTS idx_telemetry_node_time ON public.telemetry (node_id, received_at);
CREATE INDEX IF NOT EXISTS idx_telemetry_status ON public.telemetry (trash_status);

-- 3. Mengaktifkan Supabase Realtime pada tabel telemetry dan nodes
-- Agar setiap data baru otomatis di-push ke Leaflet Map di browser
ALTER PUBLICATION supabase_realtime ADD TABLE public.nodes;
ALTER PUBLICATION supabase_realtime ADD TABLE public.telemetry;

-- 4. Set RLS (Row Level Security) agar browser & backend bisa membaca/menulis
ALTER TABLE public.nodes ENABLE ROW LEVEL SECURITY;
ALTER TABLE public.telemetry ENABLE ROW LEVEL SECURITY;

-- Izinkan publik/anon membaca data
CREATE POLICY "Allow public read nodes" ON public.nodes FOR SELECT USING (true);
CREATE POLICY "Allow public read telemetry" ON public.telemetry FOR SELECT USING (true);

-- Izinkan insert/update (dengan anon key / service role)
CREATE POLICY "Allow insert update nodes" ON public.nodes FOR ALL USING (true) WITH CHECK (true);
CREATE POLICY "Allow insert telemetry" ON public.telemetry FOR ALL USING (true) WITH CHECK (true);
