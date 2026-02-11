import { promises as fs } from "node:fs";
import path from "node:path";
import { fileURLToPath } from "node:url";

const __filename = fileURLToPath(import.meta.url);
const __dirname = path.dirname(__filename);

const distDir = path.resolve(__dirname, "..", "dist");
const dataDir = path.resolve(__dirname, "..", "..", "data");
const configDir = path.resolve(__dirname, "..", "..", "config");

async function copyDir(src, dest) {
  await fs.mkdir(dest, { recursive: true });
  const entries = await fs.readdir(src, { withFileTypes: true });

  for (const entry of entries) {
    const srcPath = path.join(src, entry.name);
    const destPath = path.join(dest, entry.name);

    if (entry.isDirectory()) {
      await copyDir(srcPath, destPath);
    } else {
      await fs.copyFile(srcPath, destPath);
    }
  }
}

async function main() {
  // Clean data directory
  await fs.rm(dataDir, { recursive: true, force: true });
  await fs.mkdir(dataDir, { recursive: true });

  // Copy config.json from config/ directory
  const configSrc = path.join(configDir, "config.json");
  const configDest = path.join(dataDir, "config.json");
  await fs.copyFile(configSrc, configDest);
  console.log(`Copied config.json from ${configDir} -> ${dataDir}`);

  // Copy frontend build artifacts
  await copyDir(distDir, dataDir);
  console.log(`Copied frontend from ${distDir} -> ${dataDir}`);
}

main().catch((err) => {
  console.error(err);
  process.exit(1);
});
