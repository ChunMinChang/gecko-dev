//  qcms
//  Copyright (C) 2009 Mozilla Foundation
//  Copyright (C) 1998-2007 Marti Maria
//
// Permission is hereby granted, free of charge, to any person obtaining
// a copy of this software and associated documentation files (the "Software"),
// to deal in the Software without restriction, including without limitation
// the rights to use, copy, modify, merge, publish, distribute, sublicense,
// and/or sell copies of the Software, and to permit persons to whom the Software
// is furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO
// THE WARRANTIES OF MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE
// LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION
// OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION
// WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

use crate::{
    iccread::{curveType, Profile},
    s15Fixed16Number_to_float,
};
use crate::{matrix::Matrix, transform::PRECACHE_OUTPUT_MAX, transform::PRECACHE_OUTPUT_SIZE};

//XXX: could use a bettername
pub type uint16_fract_t = u16;

#[inline]
fn u8Fixed8Number_to_float(x: u16) -> f32 {
    // 0x0000 = 0.
    // 0x0100 = 1.
    // 0xffff = 255  + 255/256
    (x as i32 as f64 / 256.0) as f32
}
#[inline]
pub fn clamp_float(a: f32) -> f32 {
    /* One would naturally write this function as the following:
    if (a > 1.)
      return 1.;
    else if (a < 0)
      return 0;
    else
      return a;

    However, that version will let NaNs pass through which is undesirable
    for most consumers.
    */
    if a > 1. {
        1.
    } else if a >= 0. {
        a
    } else {
        // a < 0 or a is NaN
        0.
    }
}
/* value must be a value between 0 and 1 */
//XXX: is the above a good restriction to have?
// the output range of this functions is 0..1
pub fn lut_interp_linear(mut input_value: f64, table: &[u16]) -> f32 {
    if table.is_empty() {
        return input_value as f32;
    }

    input_value *= (table.len() - 1) as f64;

    let upper: i32 = input_value.ceil() as i32;
    let lower: i32 = input_value.floor() as i32;
    let value: f32 = ((table[(upper as usize).min(table.len() - 1)] as f64)
        * (1. - (upper as f64 - input_value))
        + (table[(lower as usize).min(table.len() - 1)] as f64 * (upper as f64 - input_value)))
        as f32;
    /* scale the value */
    value * (1.0 / 65535.0)
}
/* same as above but takes and returns a uint16_t value representing a range from 0..1 */
#[no_mangle]
pub fn lut_interp_linear16(input_value: u16, table: &[u16]) -> u16 {
    /* Start scaling input_value to the length of the array: 65535*(length-1).
     * We'll divide out the 65535 next */
    let mut value: u32 = (input_value as i32 * (table.len() as i32 - 1)) as u32; /* equivalent to ceil(value/65535) */
    let upper: u32 = (value + 65534) / 65535; /* equivalent to floor(value/65535) */
    let lower: u32 = value / 65535;
    /* interp is the distance from upper to value scaled to 0..65535 */
    let interp: u32 = value % 65535; // 0..65535*65535
    value = (table[upper as usize] as u32 * interp
        + table[lower as usize] as u32 * (65535 - interp))
        / 65535;
    value as u16
}
/* same as above but takes an input_value from 0..PRECACHE_OUTPUT_MAX
 * and returns a uint8_t value representing a range from 0..1 */
fn lut_interp_linear_precache_output(input_value: u32, table: &[u16]) -> u8 {
    /* Start scaling input_value to the length of the array: PRECACHE_OUTPUT_MAX*(length-1).
     * We'll divide out the PRECACHE_OUTPUT_MAX next */
    let mut value: u32 = input_value * (table.len() - 1) as u32;
    /* equivalent to ceil(value/PRECACHE_OUTPUT_MAX) */
    let upper: u32 = (value + PRECACHE_OUTPUT_MAX as u32 - 1) / PRECACHE_OUTPUT_MAX as u32;
    /* equivalent to floor(value/PRECACHE_OUTPUT_MAX) */
    let lower: u32 = value / PRECACHE_OUTPUT_MAX as u32;
    /* interp is the distance from upper to value scaled to 0..PRECACHE_OUTPUT_MAX */
    let interp: u32 = value % PRECACHE_OUTPUT_MAX as u32;
    /* the table values range from 0..65535 */
    value = table[upper as usize] as u32 * interp
        + table[lower as usize] as u32 * (PRECACHE_OUTPUT_MAX as u32 - interp); // 0..(65535*PRECACHE_OUTPUT_MAX)
                                                                                /* round and scale */
    value += (PRECACHE_OUTPUT_MAX * 65535 / 255 / 2) as u32; // scale to 0..255
    value /= (PRECACHE_OUTPUT_MAX * 65535 / 255) as u32;
    value as u8
}
/* value must be a value between 0 and 1 */
//XXX: is the above a good restriction to have?
pub fn lut_interp_linear_float(mut value: f32, table: &[f32]) -> f32 {
    value *= (table.len() - 1) as f32;

    let upper: i32 = value.ceil() as i32;
    let lower: i32 = value.floor() as i32;
    //XXX: can we be more performant here?
    value = (table[upper as usize] as f64 * (1.0f64 - (upper as f32 - value) as f64)
        + (table[lower as usize] * (upper as f32 - value)) as f64) as f32;
    /* scale the value */
    value
}

fn compute_curve_gamma_table_type1(gamma_table: &mut [f32; 256], gamma: u16) {
    let gamma_float: f32 = u8Fixed8Number_to_float(gamma);
    for (i, g) in gamma_table.iter_mut().enumerate() {
        // 0..1^(0..255 + 255/256) will always be between 0 and 1
        *g = (i as f64 / 255.0).powf(gamma_float as f64) as f32;
    }
}

fn compute_curve_gamma_table_type2(gamma_table: &mut [f32; 256], table: &[u16]) {
    for (i, g) in gamma_table.iter_mut().enumerate() {
        *g = lut_interp_linear(i as f64 / 255.0, table);
    }
}

fn compute_curve_gamma_table_type_parametric(gamma_table: &mut [f32; 256], params: &[f32]) {
    let params = Param::new(params);
    for (i, g) in gamma_table.iter_mut().enumerate() {
        let X = i as f32 / 255.;
        *g = clamp_float(params.eval(X));
    }
}

fn compute_curve_gamma_table_type0(gamma_table: &mut [f32; 256]) {
    for (i, g) in gamma_table.iter_mut().enumerate() {
        *g = (i as f64 / 255.0) as f32;
    }
}

#[inline(always)]
pub(crate) fn build_input_gamma_table(TRC: &curveType) -> [f32; 256] {
    let mut gamma_table = [0.; 256];
    match TRC {
        curveType::Parametric(params) => {
            compute_curve_gamma_table_type_parametric(&mut gamma_table, params)
        }
        curveType::Curve(data) => match data.len() {
            0 => compute_curve_gamma_table_type0(&mut gamma_table),
            1 => compute_curve_gamma_table_type1(&mut gamma_table, data[0]),
            _ => compute_curve_gamma_table_type2(&mut gamma_table, data),
        },
    };
    gamma_table
}

pub fn build_colorant_matrix(p: &Profile) -> Matrix {
    let mut result: Matrix = Matrix { m: [[0.; 3]; 3] };
    result.m[0][0] = s15Fixed16Number_to_float(p.redColorant.X);
    result.m[0][1] = s15Fixed16Number_to_float(p.greenColorant.X);
    result.m[0][2] = s15Fixed16Number_to_float(p.blueColorant.X);
    result.m[1][0] = s15Fixed16Number_to_float(p.redColorant.Y);
    result.m[1][1] = s15Fixed16Number_to_float(p.greenColorant.Y);
    result.m[1][2] = s15Fixed16Number_to_float(p.blueColorant.Y);
    result.m[2][0] = s15Fixed16Number_to_float(p.redColorant.Z);
    result.m[2][1] = s15Fixed16Number_to_float(p.greenColorant.Z);
    result.m[2][2] = s15Fixed16Number_to_float(p.blueColorant.Z);
    result
}

/** Parametric representation of transfer function */
#[derive(Debug)]
struct Param {
    g: f32,
    a: f32,
    b: f32,
    c: f32,
    d: f32,
    e: f32,
    f: f32,
}

impl Param {
    #[allow(clippy::many_single_char_names)]
    fn new(params: &[f32]) -> Param {
        // convert from the variable number of parameters
        // contained in profiles to a unified representation.
        let g: f32 = params[0];
        match params[1..] {
            [] => Param {
                g,
                a: 1.,
                b: 0.,
                c: 1.,
                d: 0.,
                e: 0.,
                f: 0.,
            },
            [a, b] => Param {
                g,
                a,
                b,
                c: 0.,
                d: -b / a,
                e: 0.,
                f: 0.,
            },
            [a, b, c] => Param {
                g,
                a,
                b,
                c: 0.,
                d: -b / a,
                e: c,
                f: c,
            },
            [a, b, c, d] => Param {
                g,
                a,
                b,
                c,
                d,
                e: 0.,
                f: 0.,
            },
            [a, b, c, d, e, f] => Param {
                g,
                a,
                b,
                c,
                d,
                e,
                f,
            },
            _ => panic!(),
        }
    }

    fn eval(&self, x: f32) -> f32 {
        if x < self.d {
            self.c * x + self.f
        } else {
            (self.a * x + self.b).powf(self.g) + self.e
        }
    }
    #[allow(clippy::many_single_char_names)]
    fn invert(&self) -> Option<Param> {
        // First check if the function is continuous at the cross-over point d.
        let d1 = (self.a * self.d + self.b).powf(self.g) + self.e;
        let d2 = self.c * self.d + self.f;

        if (d1 - d2).abs() > 0.1 {
            return None;
        }
        let d = d1;

        // y = (a * x + b)^g + e
        // y - e = (a * x + b)^g
        // (y - e)^(1/g) = a*x + b
        // (y - e)^(1/g) - b = a*x
        // (y - e)^(1/g)/a - b/a = x
        // ((y - e)/a^g)^(1/g) - b/a = x
        // ((1/(a^g)) * y - e/(a^g))^(1/g) - b/a = x
        let a = 1. / self.a.powf(self.g);
        let b = -self.e / self.a.powf(self.g);
        let g = 1. / self.g;
        let e = -self.b / self.a;

        // y = c * x + f
        // y - f = c * x
        // y/c - f/c = x
        let (c, f);
        if d <= 0. {
            c = 1.;
            f = 0.;
        } else {
            c = 1. / self.c;
            f = -self.f / self.c;
        }

        // if self.d > 0. and self.c == 0 as is likely with type 1 and 2 parametric function
        // then c and f will not be finite.
        if !(g.is_finite()
            && a.is_finite()
            && b.is_finite()
            && c.is_finite()
            && d.is_finite()
            && e.is_finite()
            && f.is_finite())
        {
            return None;
        }

        Some(Param {
            g,
            a,
            b,
            c,
            d,
            e,
            f,
        })
    }
}

#[test]
fn param_invert() {
    let p3 = Param::new(&[2.4, 0.948, 0.052, 0.077, 0.04]);
    p3.invert().unwrap();
    let g2_2 = Param::new(&[2.2]);
    g2_2.invert().unwrap();
    let g2_2 = Param::new(&[2.2, 0.9, 0.052]);
    g2_2.invert().unwrap();
    let g2_2 = dbg!(Param::new(&[2.2, 0.9, -0.52]));
    g2_2.invert().unwrap();
    let g2_2 = dbg!(Param::new(&[2.2, 0.9, -0.52, 0.1]));
    assert!(g2_2.invert().is_none());
}

/* The following code is copied nearly directly from lcms.
 * I think it could be much better. For example, Argyll seems to have better code in
 * icmTable_lookup_bwd and icmTable_setup_bwd. However, for now this is a quick way
 * to a working solution and allows for easy comparing with lcms. */
#[no_mangle]
#[allow(clippy::many_single_char_names)]
pub fn lut_inverse_interp16(Value: u16, LutTable: &[u16]) -> uint16_fract_t {
    let mut l: i32 = 1; // 'int' Give spacing for negative values
    let mut r: i32 = 0x10000;
    let mut x: i32 = 0;
    let mut res: i32;
    let length = LutTable.len() as i32;

    let mut NumZeroes: i32 = 0;
    while LutTable[NumZeroes as usize] as i32 == 0 && NumZeroes < length - 1 {
        NumZeroes += 1
    }
    // There are no zeros at the beginning and we are trying to find a zero, so
    // return anything. It seems zero would be the less destructive choice
    /* I'm not sure that this makes sense, but oh well... */
    if NumZeroes == 0 && Value as i32 == 0 {
        return 0u16;
    }
    let mut NumPoles: i32 = 0;
    while LutTable[(length - 1 - NumPoles) as usize] as i32 == 0xffff && NumPoles < length - 1 {
        NumPoles += 1
    }
    // Does the curve belong to this case?
    if NumZeroes > 1 || NumPoles > 1 {
        let a_0: i32;
        let b_0: i32;
        // Identify if value fall downto 0 or FFFF zone
        if Value as i32 == 0 {
            return 0u16;
        }
        // if (Value == 0xFFFF) return 0xFFFF;
        // else restrict to valid zone
        if NumZeroes > 1 {
            a_0 = (NumZeroes - 1) * 0xffff / (length - 1);
            l = a_0 - 1
        }
        if NumPoles > 1 {
            b_0 = (length - 1 - NumPoles) * 0xffff / (length - 1);
            r = b_0 + 1
        }
    }
    if r <= l {
        // If this happens LutTable is not invertible
        return 0u16;
    }
    // Seems not a degenerated case... apply binary search
    while r > l {
        x = (l + r) / 2;
        res = lut_interp_linear16((x - 1) as uint16_fract_t, LutTable) as i32;
        if res == Value as i32 {
            // Found exact match.
            return (x - 1) as uint16_fract_t;
        }
        if res > Value as i32 {
            r = x - 1
        } else {
            l = x + 1
        }
    }

    // Not found, should we interpolate?

    // Get surrounding nodes
    debug_assert!(x >= 1);

    let val2: f64 = (length - 1) as f64 * ((x - 1) as f64 / 65535.0);
    let cell0: i32 = val2.floor() as i32;
    let cell1: i32 = val2.ceil() as i32;
    if cell0 == cell1 {
        return x as uint16_fract_t;
    }

    let y0: f64 = LutTable[cell0 as usize] as f64;
    let x0: f64 = 65535.0 * cell0 as f64 / (length - 1) as f64;
    let y1: f64 = LutTable[cell1 as usize] as f64;
    let x1: f64 = 65535.0 * cell1 as f64 / (length - 1) as f64;
    let a: f64 = (y1 - y0) / (x1 - x0);
    let b: f64 = y0 - a * x0;
    if a.abs() < 0.01f64 {
        return x as uint16_fract_t;
    }
    let f: f64 = (Value as i32 as f64 - b) / a;
    if f < 0.0 {
        return 0u16;
    }
    if f >= 65535.0 {
        return 0xffffu16;
    }
    (f + 0.5f64).floor() as uint16_fract_t
}
/*
The number of entries needed to invert a lookup table should not
necessarily be the same as the original number of entries.  This is
especially true of lookup tables that have a small number of entries.

For example:
Using a table like:
   {0, 3104, 14263, 34802, 65535}
invert_lut will produce an inverse of:
   {3, 34459, 47529, 56801, 65535}
which has an maximum error of about 9855 (pixel difference of ~38.346)

For now, we punt the decision of output size to the caller. */
fn invert_lut(table: &[u16], out_length: usize) -> Vec<u16> {
    /* for now we invert the lut by creating a lut of size out_length
     * and attempting to lookup a value for each entry using lut_inverse_interp16 */
    let mut output = Vec::with_capacity(out_length);
    for i in 0..out_length {
        let x: f64 = i as f64 * 65535.0 / (out_length - 1) as f64;
        let input: uint16_fract_t = (x + 0.5f64).floor() as uint16_fract_t;
        output.push(lut_inverse_interp16(input, table));
    }
    output
}
#[allow(clippy::needless_range_loop)]
fn compute_precache_pow(output: &mut [u8; PRECACHE_OUTPUT_SIZE], gamma: f32) {
    for v in 0..PRECACHE_OUTPUT_SIZE {
        //XXX: don't do integer/float conversion... and round?
        output[v] = (255. * (v as f32 / PRECACHE_OUTPUT_MAX as f32).powf(gamma)) as u8;
    }
}
#[allow(clippy::needless_range_loop)]
pub fn compute_precache_lut(output: &mut [u8; PRECACHE_OUTPUT_SIZE], table: &[u16]) {
    for v in 0..PRECACHE_OUTPUT_SIZE {
        output[v] = lut_interp_linear_precache_output(v as u32, table);
    }
}
#[allow(clippy::needless_range_loop)]
pub fn compute_precache_linear(output: &mut [u8; PRECACHE_OUTPUT_SIZE]) {
    for v in 0..PRECACHE_OUTPUT_SIZE {
        //XXX: round?
        output[v] = (v / (PRECACHE_OUTPUT_SIZE / 256)) as u8;
    }
}
pub(crate) fn compute_precache(trc: &curveType, output: &mut [u8; PRECACHE_OUTPUT_SIZE]) {
    match trc {
        curveType::Parametric(params) => {
            let mut gamma_table_uint: [u16; 256] = [0; 256];

            let mut inverted_size: usize = 256;
            let mut gamma_table = [0.; 256];
            compute_curve_gamma_table_type_parametric(&mut gamma_table, params);
            let mut i: u16 = 0u16;
            while (i as i32) < 256 {
                gamma_table_uint[i as usize] = (gamma_table[i as usize] * 65535f32) as u16;
                i += 1
            }
            //XXX: the choice of a minimum of 256 here is not backed by any theory,
            //     measurement or data, however it is what lcms uses.
            //     the maximum number we would need is 65535 because that's the
            //     accuracy used for computing the pre cache table
            if inverted_size < 256 {
                inverted_size = 256
            }
            let inverted = invert_lut(&gamma_table_uint, inverted_size);
            compute_precache_lut(output, &inverted);
        }
        curveType::Curve(data) => {
            match data.len() {
                0 => compute_precache_linear(output),
                1 => compute_precache_pow(output, 1. / u8Fixed8Number_to_float(data[0])),
                _ => {
                    let mut inverted_size = data.len();
                    //XXX: the choice of a minimum of 256 here is not backed by any theory,
                    //     measurement or data, however it is what lcms uses.
                    //     the maximum number we would need is 65535 because that's the
                    //     accuracy used for computing the pre cache table
                    if inverted_size < 256 {
                        inverted_size = 256
                    } //XXX turn this conversion into a function
                    let inverted = invert_lut(data, inverted_size);
                    compute_precache_lut(output, &inverted);
                }
            }
        }
    }
}
fn build_linear_table(length: usize) -> Vec<u16> {
    let mut output = Vec::with_capacity(length);
    for i in 0..length {
        let x: f64 = i as f64 * 65535.0 / (length - 1) as f64;
        let input: uint16_fract_t = (x + 0.5f64).floor() as uint16_fract_t;
        output.push(input);
    }
    output
}
fn build_pow_table(gamma: f32, length: usize) -> Vec<u16> {
    let mut output = Vec::with_capacity(length);
    for i in 0..length {
        let mut x: f64 = i as f64 / (length - 1) as f64;
        x = x.powf(gamma as f64);
        let result: uint16_fract_t = (x * 65535.0 + 0.5f64).floor() as uint16_fract_t;
        output.push(result);
    }
    output
}

fn to_lut(params: &Param, len: usize) -> Vec<u16> {
    let mut output = Vec::with_capacity(len);
    for i in 0..len {
        let X = i as f32 / (len-1) as f32;
        output.push((params.eval(X) * 65535.) as u16);
    }
    output
}

pub(crate) fn build_lut_for_linear_from_tf(trc: &curveType,
        lut_len: Option<usize>) -> Vec<u16> {
    match trc {
        curveType::Parametric(params) => {
            let lut_len = lut_len.unwrap_or(256);
            let params = Param::new(params);
            to_lut(&params, lut_len)
        },
        curveType::Curve(data) => {
            let autogen_lut_len = lut_len.unwrap_or(4096);
            match data.len() {
                0 => build_linear_table(autogen_lut_len),
                1 => {
                    let gamma = u8Fixed8Number_to_float(data[0]);
                    build_pow_table(gamma, autogen_lut_len)
                }
                _ => {
                    let lut_len = lut_len.unwrap_or(data.len());
                    assert_eq!(lut_len, data.len());
                    data.clone() // I feel bad about this.
                }
            }
        },
    }
}

pub(crate) fn build_lut_for_tf_from_linear(trc: &curveType) -> Option<Vec<u16>> {
    match trc {
        curveType::Parametric(params) => {
            let lut_len = 256;
            let params = Param::new(params);
            if let Some(inv_params) = params.invert() {
                return Some(to_lut(&inv_params, lut_len));
            }
            // else return None instead of fallthrough to generic lut inversion.
            return None;
        },
        curveType::Curve(data) => {
            let autogen_lut_len = 4096;
            match data.len() {
                0 => {
                    return Some(build_linear_table(autogen_lut_len));
                },
                1 => {
                    let gamma = 1. / u8Fixed8Number_to_float(data[0]);
                    return Some(build_pow_table(gamma, autogen_lut_len));
                },
                _ => {},
            }
        },
    }

    let linear_from_tf = build_lut_for_linear_from_tf(trc, None);

    //XXX: the choice of a minimum of 256 here is not backed by any theory,
    //     measurement or data, however it is what lcms uses.
    let inverted_lut_len = std::cmp::max(linear_from_tf.len(), 256);
    Some(invert_lut(&linear_from_tf, inverted_lut_len))
}

pub(crate) fn build_output_lut(trc: &curveType) -> Option<Vec<u16>> {
    build_lut_for_tf_from_linear(trc)
}
