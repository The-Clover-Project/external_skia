diagnostic(off, derivative_uniformity);
diagnostic(off, chromium.unreachable_code);
struct FSOut {
  @location(0) sk_FragColor: vec4<f32>,
};
struct _GlobalUniforms {
  src: vec4<f32>,
  dst: vec4<f32>,
};
@binding(0) @group(0) var<uniform> _globalUniforms: _GlobalUniforms;
fn _skslMain(_stageOut: ptr<function, FSOut>) {
  {
    var _0_a: vec4<f32> = _globalUniforms.src + (1.0 - _globalUniforms.src.w) * _globalUniforms.dst;
    let _1_b: vec3<f32> = (1.0 - _globalUniforms.dst.w) * _globalUniforms.src.xyz + _globalUniforms.dst.xyz;
    let _skTemp0 = min(_0_a.xyz, _1_b);
    _0_a = vec4<f32>((_skTemp0), _0_a.w);
    (*_stageOut).sk_FragColor = _0_a;
  }
}
@fragment fn main() -> FSOut {
  var _stageOut: FSOut;
  _skslMain(&_stageOut);
  return _stageOut;
}
