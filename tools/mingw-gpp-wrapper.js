const { spawnSync } = require("node:child_process");
const path = require("node:path");

const compiler = process.env.DBMS_MINGW_GXX || "C:\\msys64\\ucrt64\\bin\\g++.exe";
const args = process.argv.slice(2).map((arg) => arg.replace(/\\/g, "/"));
const compilerDir = path.dirname(compiler);
const env = {
  ...process.env,
  PATH: `${compilerDir};${process.env.PATH || ""}`,
};

const result = spawnSync(compiler, args, { stdio: "inherit", env });
if (result.error) {
  console.error(result.error.message);
  process.exit(1);
}
process.exit(result.status ?? 1);
