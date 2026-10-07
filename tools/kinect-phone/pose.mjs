// Prototype coordinates, not Kinect depth or sensor-space data.
const mapping = [[23,24],[11,12],[11,12],[7,8],11,13,15,19,12,14,16,20,23,25,27,31,24,26,28,32];
export function skeleton(points, timestamp) {
  if (!Array.isArray(points) || points.length !== 33 || !Number.isFinite(timestamp)) throw Error('Invalid pose');
  const joints = mapping.map(index => {
    const ids = Array.isArray(index) ? index : [index];
    const values = ids.map(i => points[i]);
    if (values.some(p => !p || ![p.x,p.y,p.z].every(Number.isFinite))) throw Error('Invalid joint');
    return {x:values.reduce((n,p)=>n+p.x,0)/ids.length,
      y:-values.reduce((n,p)=>n+p.y,0)/ids.length,
      z:-values.reduce((n,p)=>n+p.z,0)/ids.length,
      confidence:Math.max(0,Math.min(1,Math.min(...values.map(p=>p.visibility??0))))};
  });
  for (const axis of ['x','y','z']) joints[1][axis] = (joints[0][axis]+joints[2][axis])/2;
  return {protocol:'ps5x360-phone-pose',version:1,timestamp,space:'hip-relative-estimated-metres',joints};
}
