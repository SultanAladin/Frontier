/*==========================================================================================================================================
                                                       SCULPTEVIDENCEVALIDATION.MJS
==========================================================================================================================================*/
// 🧩 Audit retained sculpt evidence, source and built bytes, without rerunning tests or hiding the unresolved editor failure.

import Assert from 'node:assert/strict';
import { readFile as ReadFile, readdir as ReadDirectory, writeFile as WriteFile } from 'node:fs/promises';
import { createHash as CreateHash } from 'node:crypto';
import { execFileSync as ExecuteFile } from 'node:child_process';

const Directory = new URL('./', import.meta.url), Application = new URL('../../Experimental/LandscapeWorkspace/', Directory);
const Digest = Bytes => CreateHash('sha256').update(Bytes).digest('hex');
const ReadReport = async Name => JSON.parse(await ReadFile(new URL('Results/' + Name + '.json', Directory), 'utf8'));
const Results = {
    Scope: 'Evidence integrity only. Does not rerun browser or unit tests and does not certify GPU stability.',
    Date: new Date().toISOString(),
    Branch: ExecuteFile('git', ['branch', '--show-current'], { encoding: 'utf8' }).trim(),
    Revision: ExecuteFile('git', ['rev-parse', 'HEAD'], { encoding: 'utf8' }).trim(),
    EvidenceIntegrity: 'running', AcceptanceStatus: 'blocked', Checks: [], ArtifactSHA256: {}, UnitSourceSHA256: {}
};

//------------------------------------------------------------------------------------------------------------------------
//                                                       CURRENT REPORTS AND BYTES
//------------------------------------------------------------------------------------------------------------------------

try
{
    Assert.equal(Results.Branch, 'arena/ceeabe45-frontier');
    const Expected = {
        GpuSculpt: 10, ProductionGpuSculpt: 10, GpuSurfaceValidation: 12, ProductionGpuSurface: 12,
        GpuRegionResolution: 10, ProductionGpuRegionResolution: 10, GpuPreviewSampling: 6, ProductionGpuPreviewSampling: 6,
        GpuQualityValidation: 3, ProductionGpuQuality: 3, GpuFailureValidation: 5, ProductionGpuFailure: 5,
        MetricBrowserValidation: 7, ProductionValidation: 3
    };
    const Names = [...Object.keys(Expected), 'MetricDistanceValidation', 'HighResolutionValidation',
        'GpuDialogEditorOnly', 'BrowserValidation', 'GpuEditorSculptBatchFailure', 'GpuEditorImportWaitFailure'];
    let SourceComparisons = 0;
    for (const Name of Names)
    {
        const Report = await ReadReport(Name), Bytes = await ReadFile(new URL('Results/' + Name + '.json', Directory));
        Results.ArtifactSHA256[Name + '.json'] = Digest(Bytes);
        if (Name in Expected)
        {
            Assert.equal(Report.Status, 'passed', Name);
            Assert.equal(Report.Checks.length, Expected[Name], Name);
        }
        for (const [Path, ExpectedHash] of Object.entries(Report.SourceSHA256 || {}))
        {
            Assert.equal(Digest(await ReadFile(new URL(Path, Application))), ExpectedHash, Name + ': ' + Path);
            SourceComparisons++;
        }
        for (const Image of Report.Images || []) Assert.ok((await ReadFile(new URL('Images/' + Image, Directory))).length > 0);
    }
    Results.Checks.push({ Name: 'Current report fingerprints and retained image paths', SourceComparisons, Reports: Names.length });

    const Production = await ReadReport('ProductionValidation');
    for (const Asset of Production.Assets)
        Assert.equal(Digest(await ReadFile(new URL('dist/' + Asset.Path, Application))), Asset.SHA256, Asset.Path);
    Results.Checks.push({ Name: 'Exact production assets', Count: Production.Assets.length });

    const Tap = await ReadFile(new URL('Results/UnitTests.tap', Directory), 'utf8');
    Assert.match(Tap, /# tests 183\b/); Assert.match(Tap, /# pass 183\b/); Assert.match(Tap, /# fail 0\b/);
    const Build = await ReadFile(new URL('Results/ProductionBuild.txt', Directory), 'utf8');
    Assert.match(Build, /2044 modules transformed/);
    for (const Name of await ReadDirectory(Application))
        if (Name.endsWith('.test.js')) Results.UnitSourceSHA256[Name] = Digest(await ReadFile(new URL(Name, Application)));
    Results.ArtifactSHA256['UnitTests.tap'] = Digest(Tap);
    Results.ArtifactSHA256['ProductionBuild.txt'] = Digest(Build);
    Results.Checks.push({ Name: 'Retained unit/build results', Units: 183, BuildModules: 2044 });

    const Closure = new Set();
    async function EnumerateImports(Name)
    {
        if (Closure.has(Name)) return;
        Closure.add(Name);
        const Source = await ReadFile(new URL(Name, Application), 'utf8');
        for (const Match of Source.matchAll(/(?:from\s*|import\s*)['"]\.\/([^'"]+\.js)['"]/g)) await EnumerateImports(Match[1]);
    }
    await EnumerateImports('VolumeSubmission.js');
    Assert.ok(Closure.has('VolumeSculpt.js') && Closure.has('VolumeRefinement.js'));
    Assert.equal(Closure.has('VolumeScalarRasterization.js'), false);
    Results.Checks.push({ Name: 'Live graphics import closure excludes the historical CPU pixel renderer', Files: [...Closure].sort() });

//------------------------------------------------------------------------------------------------------------------------
//                                                       HONEST ACCEPTANCE BOUNDARY
//------------------------------------------------------------------------------------------------------------------------

    const Volume = await ReadReport('HighResolutionValidation');
    Assert.equal(Volume.Status, 'passed'); Assert.deepEqual(Volume.Dimensions, [1024, 128, 1024]);
    Assert.equal(Volume.Redistance.SignChanges, 0);
    const Numeric = await ReadReport('MetricDistanceValidation'); Assert.equal(Numeric.Status, 'passed');
    Assert.equal(Numeric.Fixtures.length, 6); Assert.equal(Numeric.Refinement.length, 3);
    const Resume = await ReadReport('GpuDialogEditorOnly'); Assert.equal(Resume.Status, 'passed');

    const Editor = await ReadReport('BrowserValidation'), Original = await ReadReport('GpuEditorSculptBatchFailure');
    Assert.equal(Editor.Status, 'failed'); Assert.equal(Original.Status, 'failed');
    Assert.match(Editor.PreviewAtFailure.renderMessage, /four seconds/);
    Assert.match(Original.PreviewAtFailure.renderMessage, /four seconds/);
    Assert.equal(Editor.PreviewAtFailure.surfaceTriangles, '0');
    Assert.ok((await ReadFile(new URL('Images/GpuEditorSculptFailure.png', Directory))).length > 0);
    Results.Blocker = {
        Name: 'Broad editor GPU fence stop remains unresolved', PassedBeforeFailure: Editor.Checks.length,
        Report: 'BrowserValidation.json', OriginalBatch: 'GpuEditorSculptBatchFailure.json',
        LatestMessage: Editor.PreviewAtFailure.renderMessage,
        PhysicalWindowsGpuVerification: 'not run', NoDeadlineOverride: true, NoSoftwareFallback: true
    };
    Results.EvidenceIntegrity = 'passed';
}
catch (Failure)
{
    Results.EvidenceIntegrity = 'failed'; Results.Failure = Failure.stack; process.exitCode = 1;
}
await WriteFile(new URL('Results/SculptEvidenceAudit.json', Directory), JSON.stringify(Results, null, 2) + '\n');
console.log(JSON.stringify(Results, null, 2));
