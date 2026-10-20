// Restore the npm-distributed Chromium support libraries on minimal Linux editing hosts.
import fs from "node:fs";
import path from "node:path";
import zlib from "node:zlib";
import { fileURLToPath } from "node:url";
import { spawnSync } from "node:child_process";
import Chromium from "@sparticuz/chromium";

const Tools = path.dirname(fileURLToPath(import.meta.url));
const Environment = { ...process.env };
if (process.platform === "linux" && !process.env.CHROMIUM_EXECUTABLE)
{
    const LibraryDirectory = path.join(Tools, ".browser");
    fs.mkdirSync(LibraryDirectory, { recursive: true });
    const PackageDirectory = path.resolve(path.dirname(fileURLToPath(import.meta.resolve("@sparticuz/chromium"))), "..");
    const Archive = path.join(LibraryDirectory, "al2023.tar");
    fs.writeFileSync(Archive, zlib.brotliDecompressSync(fs.readFileSync(path.join(PackageDirectory, "bin/al2023.tar.br"))));
    const Extraction = spawnSync("tar", ["xf", Archive, "-C", LibraryDirectory], { stdio: "inherit" });
    if (Extraction.status !== 0) process.exit(Extraction.status || 1);
    fs.unlinkSync(Archive);
    const Executable = await Chromium.executablePath();
    Environment.CHROMIUM_EXECUTABLE = Executable;
    Environment.LD_LIBRARY_PATH = [path.dirname(Executable), path.join(LibraryDirectory, "lib"), process.env.LD_LIBRARY_PATH].filter(Boolean).join(":");
}
else if (!process.env.CHROMIUM_EXECUTABLE)
{
    throw new Error("Set CHROMIUM_EXECUTABLE to an installed Chrome/Edge binary on this platform");
}
const Result = spawnSync(process.execPath, [path.resolve(Tools, process.env.BROWSER_VALIDATION || "BrowserValidation.mjs")],
    { cwd: Tools, env: Environment, stdio: "inherit" });
if (Result.error) throw Result.error;
process.exit(Result.status === null ? 1 : Result.status);
