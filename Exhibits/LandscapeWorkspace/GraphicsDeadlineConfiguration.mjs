/*==========================================================================================================================================
                                                     GRAPHICSDEADLINECONFIGURATION.MJS
==========================================================================================================================================*/
// 🧩 Explicit extended compiler allowance for GPU-only validation, separate from the unchanged production image deadline.

import Assert from 'node:assert/strict';
import { createHash } from 'node:crypto';

export async function ConfigureGraphicsDeadline(Page, Results, Milliseconds = 90000)
{
    Results.GraphicsDeadlineConfiguration = {
        Milliseconds,
        Note: 'GPU-only lifecycle/900px quality validation deliberately allows a slow software GPU to finish. This HTTP-only constructor-option override is NOT production behaviour. Default 12-second recovery is independently exercised unmodified in PreviewDeadlineValidation and ProductionPreviewDeadline.'
    };
    await Page.route('**/Workspace.js*', async Route =>
    {
        const Response = await Route.fetch(), Original = await Response.text();
        const Needle = 'CreateVolumePresentation(Volume, {';
        Assert.equal(Original.split(Needle).length, 2);
        const Modified = Original.replace(Needle, Needle + ' GraphicsImageTimeoutMilliseconds: ' + Milliseconds + ',');
        Results.GraphicsDeadlineConfiguration.OriginalHTTPSourceSHA256 = createHash('sha256').update(Original).digest('hex');
        Results.GraphicsDeadlineConfiguration.ModifiedHTTPSourceSHA256 = createHash('sha256').update(Modified).digest('hex');
        await Route.fulfill({ response: Response, body: Modified });
    });
}
