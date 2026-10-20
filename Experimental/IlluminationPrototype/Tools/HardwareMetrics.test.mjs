import test from 'node:test';
import assert from 'node:assert/strict';
import {HardwareMetrics} from '../Source/HardwareMetrics.js';

test('requesting timestamps for an omitted/empty pass does not reuse stale GPU queries',()=>{
 const Metrics=new HardwareMetrics({TimestampEnabled:false},['Union','Direct']);
 Metrics.Enabled=true;Metrics.QuerySet={};
 const Empty=Metrics.QueryWrites('Union');
 assert.equal(Metrics.Written.size,0);
 const Direct=Metrics.QueryWrites('Direct');
 // Simulate the WebGPU dictionary conversion at Encoder.beginComputePass.
 assert.equal(Direct.querySet,Metrics.QuerySet);
 assert.equal(Direct.beginningOfPassWriteIndex,2);
 assert.deepEqual([...Metrics.Written],['Direct']);
 assert.equal(Empty.endOfPassWriteIndex,1);
});
