#!/usr/bin/env bash
# 🧩 Proves the renamed Project Drive core in Frontier/Engine/PhysicalDynamics/Vehicle/ behaves exactly like its source:
#    SultanAladin/Frontier- at 3f14808 (Engine/PhysicalDynamics/Vehicle/). Both cores are built with the seven upstream
#    suites and the force/slip/drivetrain trace; every number the 16 programs print must match, in order.
set -uo pipefail
ROOT="$(cd "$(dirname "$0")/../.." && pwd)"
cd "$ROOT"
ORIGIN="$ROOT/_ClaudeScratch/ProjectDriveOrigin"
BUILD="$ROOT/_ClaudeScratch/ProjectDriveIdentity"
RESULTS="${RESULTS:-$ROOT/Exhibits/VehiclePhysics/Results/ProjectDriveIdentity.txt}"
SOURCE_COMMIT=3f14808a6dcc909ae64070d62639a7019f5de794
TRACE_COMMIT=56059b393f2d1a6f4ecf73c0f7661aa74f2de442
CXX="${CXX:-g++}"
mkdir -p "$BUILD/original" "$BUILD/renamed" "$(dirname "$RESULTS")"

# 📝 The source and the trace program, fetched once (sparse, blobs on demand).
if [[ ! -f "$ORIGIN/source/Engine/PhysicalDynamics/Vehicle/VehicleSolver.cpp" ]]; then
    rm -rf "$ORIGIN"; mkdir -p "$ORIGIN"
    git -C "$ORIGIN" init -q source && git -C "$ORIGIN/source" remote add origin https://github.com/SultanAladin/Frontier-.git
    git -C "$ORIGIN/source" sparse-checkout set --no-cone /Engine/PhysicalDynamics/Vehicle/
    git -C "$ORIGIN/source" fetch -q --depth=1 --filter=blob:none origin "$SOURCE_COMMIT" && git -C "$ORIGIN/source" checkout -q FETCH_HEAD
    git -C "$ORIGIN" init -q trace && git -C "$ORIGIN/trace" remote add origin https://github.com/streamlinkinbox/Frontier.git
    git -C "$ORIGIN/trace" sparse-checkout set --no-cone /Exhibits/VehiclePhysics/Code/ProjectDriveTrace.cpp
    git -C "$ORIGIN/trace" fetch -q --depth=1 --filter=blob:none origin "$TRACE_COMMIT" && git -C "$ORIGIN/trace" checkout -q FETCH_HEAD
fi
ORIGINAL="$ORIGIN/source/Engine/PhysicalDynamics/Vehicle"
RENAMED="$ROOT/Frontier/Engine/PhysicalDynamics/Vehicle"
SUITES="$ROOT/Exhibits/VehiclePhysics/Code/ProjectDrive"
FLAGS=(-std=c++17 -O2 -w)
CORE=(Aerodynamics Drivetrain PacejkaMagicFormula TyreSlipDynamics VehicleGeometry VehicleSolver)
# 📝 The renamed core also carries the soft tyre carcasses (off by default, so the comparison still holds).
EXTRA=(TyreCarcassLattice TyreCarcassSolid)
# 📝 original suite → renamed suite
PAIRS=("VehicleValidation VehicleValidation" "VehicleDrivingChecks VehicleDrivingValidation" "AbsSuperchargerChecks AbsSuperchargerValidation"
       "AerodynamicsValidation AerodynamicsValidation" "BrakingValidation BrakingValidation" "SuperchargerValidation SuperchargerValidation"
       "VehicleInputChecks VehicleInputValidation" "ProjectDriveTrace ProjectDriveTrace")
build_side() {
    local SIDE=$1 CORE_DIR=$2 OBJECTS=()
    local UNITS=("${CORE[@]}")
    [[ $SIDE == renamed ]] && UNITS+=("${EXTRA[@]}")
    for UNIT in "${UNITS[@]}"; do
        "$CXX" "${FLAGS[@]}" -I "$CORE_DIR" -c "$CORE_DIR/$UNIT.cpp" -o "$BUILD/$SIDE/$UNIT.o" || return 1
        OBJECTS+=("$BUILD/$SIDE/$UNIT.o")
    done
    for PAIR in "${PAIRS[@]}"; do
        read -r OLD NEW <<< "$PAIR"
        local PROGRAM
        if [[ $SIDE == original ]]; then
            PROGRAM="$ORIGINAL/$OLD.cpp"; [[ $OLD == ProjectDriveTrace ]] && PROGRAM="$ORIGIN/trace/Exhibits/VehiclePhysics/Code/ProjectDriveTrace.cpp"
        else
            PROGRAM="$SUITES/$NEW.cpp"
        fi
        "$CXX" "${FLAGS[@]}" -I "$CORE_DIR" "$PROGRAM" "${OBJECTS[@]}" -o "$BUILD/$SIDE/$OLD" || return 1
        "$BUILD/$SIDE/$OLD" > "$BUILD/$SIDE/$OLD.out" 2> "$BUILD/$SIDE/$OLD.err"
    done
}
build_side original "$ORIGINAL" || { echo "original build failed"; exit 1; }
build_side renamed "$RENAMED" || { echo "renamed build failed"; exit 1; }
{
    echo "Project Drive identity: SultanAladin/Frontier- $SOURCE_COMMIT against Frontier/Engine/PhysicalDynamics/Vehicle"
    "$CXX" --version | head -1
    python3 - "$BUILD" <<'PY'
import re, sys
build = sys.argv[1]
names = ['VehicleValidation','VehicleDrivingChecks','AbsSuperchargerChecks','AerodynamicsValidation','BrakingValidation',
         'SuperchargerValidation','VehicleInputChecks','ProjectDriveTrace']
token = re.compile(r'[-+]?(?:\d+\.\d*|\.\d+|\d+)(?:[eE][-+]?\d+)?|PASS|FAIL|nan|inf')
same_all = True
for name in names:
    for stream in ('out', 'err'):
        a = token.findall(open(f'{build}/original/{name}.{stream}').read())
        b = token.findall(open(f'{build}/renamed/{name}.{stream}').read())
        same = a == b; same_all &= same
        passes = b.count('PASS'); fails = b.count('FAIL')
        print(f'{name:24s} {stream}: {len(a):7d} numbers  identical={"yes" if same else "NO"}  PASS={passes} FAIL={fails}')
print('RESULT: every number identical' if same_all else 'RESULT: DIFFERENCES FOUND')
sys.exit(0 if same_all else 1)
PY
} | tee "$RESULTS"
exit "${PIPESTATUS[0]}"
