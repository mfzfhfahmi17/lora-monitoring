CREATE DATABASE IF NOT EXISTS lora_monitoring
CHARACTER SET utf8mb4
COLLATE utf8mb4_unicode_ci;

USE lora_monitoring;

CREATE TABLE IF NOT EXISTS nodes (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  node_id VARCHAR(100) NOT NULL,
  node_name VARCHAR(150) NULL,
  latitude DOUBLE NULL,
  longitude DOUBLE NULL,
  trash_status VARCHAR(20) NOT NULL DEFAULT 'TIDAK ADA',
  servo_angle DECIMAL(6,2) NULL,
  object_distance_cm DECIMAL(10,2) NULL,
  rssi_dbm INT NULL,
  sender_time VARCHAR(30) NULL,
  last_seen_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  created_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  updated_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP ON UPDATE CURRENT_TIMESTAMP,
  PRIMARY KEY (id),
  UNIQUE KEY uq_nodes_node_id (node_id),
  KEY idx_nodes_last_seen (last_seen_at)
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;

CREATE TABLE IF NOT EXISTS telemetry (
  id BIGINT UNSIGNED NOT NULL AUTO_INCREMENT,
  node_id VARCHAR(100) NOT NULL,
  sender_time VARCHAR(30) NULL,
  latitude DOUBLE NOT NULL,
  longitude DOUBLE NOT NULL,
  trash_status VARCHAR(20) NOT NULL,
  servo_angle DECIMAL(6,2) NULL,
  object_distance_cm DECIMAL(10,2) NULL,
  rssi_dbm INT NULL,
  received_at TIMESTAMP NOT NULL DEFAULT CURRENT_TIMESTAMP,
  raw_payload TEXT NULL,
  PRIMARY KEY (id),
  KEY idx_telemetry_node_id (node_id),
  KEY idx_telemetry_received_at (received_at),
  KEY idx_telemetry_node_time (node_id, received_at),
  KEY idx_telemetry_status (trash_status),
  CONSTRAINT fk_telemetry_node
    FOREIGN KEY (node_id) REFERENCES nodes(node_id)
    ON UPDATE CASCADE ON DELETE CASCADE
) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4 COLLATE=utf8mb4_unicode_ci;
