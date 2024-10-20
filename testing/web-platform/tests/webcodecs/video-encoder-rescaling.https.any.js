// META: global=window,dedicatedworker
// META: variant=?av1
// META: variant=?vp8
// META: variant=?h264_avc

let BASECONFIG = null;
promise_setup(async () => {
    const config = {
        '?av1': { codec: 'av01.0.04M.08' },
        '?vp8': { codec: 'vp8', avc: { format: 'annexb' }, bitrateMode: 'variable', framerate: 60, bitrate: 3000000, 'scalabilityMode': 'L1T2', latencyMode: 'quality' },
        '?h264_avc': { codec: 'avc1.42402A', avc: { format: 'annexb' }, bitrate: 3000000 },
    }[location.search];
    BASECONFIG = config;
});

function executeCodecsOnce(oneFrame, scaleSize) {
    return new Promise(async (resolve, reject) => {
        let decoder
        let done = false
        const encoder = new VideoEncoder({
            output: (frame, mdata) => {
                decoder.decode(frame)
            },
            error: (error) => {
                if (!done) reject(error)
            }
        })
        decoder = new VideoDecoder({
            output: (frame) => {
                done = true
                encoder.close()
                decoder.close()
                resolve(frame)
            },
            error: (error) => {
                if (!done) reject(error)
            }
        })
        const config = {
            ...BASECONFIG,
            displayWidth: scaleSize.x,
            displayHeight: scaleSize.y,
            width: scaleSize.x,
            height: scaleSize.y,
        }
        // reject(new Error(JSON.stringify(config)))
        encoder.configure(config);
        decoder.configure(config);
        encoder.encode(oneFrame);
        encoder.flush().catch((error) => {
            if (!done) reject(error)
        })
    });
}

// This looks very complicated, but should allow, to almost cover every pixel format
// but is probably not the fastest possible implementation
function createImageData({ channelOffsets, channelWidths, channelPlaneWidths, channelStrides, channelSteps, channelHeights, channelFourColors }) {
    let memSize = 0;
    for (let chan = 0; chan < 3; chan++) {
        memSize += channelHeights[chan] * channelPlaneWidths[chan];
    }
    //assert_equals(0,2,"mem_size" +memSize);
    //assert_equals(0,2, "Debug:" + channelOffsets +';'+ channelWidths +';'+ channelPlaneWidths +';' + channelStrides +';'+  channelSteps +';'+  channelHeights );
    let data = new Uint8Array(memSize);
    for (let chan = 0; chan < 3; chan++) {
        for (let y = 0; y < channelHeights[chan]; y++) {
            const yInd = (2 * y < channelHeights[chan]) ? 1 : 0;
            for (let x = 0; x < channelWidths[chan]; x++) {
                const xInd = (2 * x < channelWidths[chan]) ? 2 : 0;
                data[channelOffsets[chan] + Math.floor(channelStrides[chan] * y) + Math.floor(channelSteps[chan] * x)] = channelFourColors[xInd + yInd][chan];
            }
        }
    }
    return data;
}

function testImageData(data, { channelOffsets, channelWidths, channelStrides, channelSteps, channelHeights, channelFourColors }) {
    let err = 0.;
    for (let chan = 0; chan < 3; chan++) {
        for (let y = 0; y < channelHeights[chan]; y++) {
            const yInd = (2 * y < channelHeights[chan]) ? 1 : 0;
            for (let x = 0; x < channelWidths[chan]; x++) {
                const xInd = (2 * x < channelWidths[chan]) ? 2 : 0;
                const curdata = data[channelOffsets[chan] + Math.floor(channelStrides[chan] * y) + Math.floor(channelSteps[chan] * x)];
                const diff = curdata - channelFourColors[xInd + yInd][chan];
                err += Math.abs(diff);
            }
        }
    }
    return err / data.length / 3 / 255 * 4;
}

function rgb2yuv(rgb) {
    let y = rgb[0] * .299000 + rgb[1] * .587000 + rgb[2] * .114000
    let u = rgb[0] * -.168736 + rgb[1] * -.331264 + rgb[2] * .500000 + 128
    let v = rgb[0] * .500000 + rgb[1] * -.418688 + rgb[2] * -.081312 + 128

    y = Math.floor(y);
    u = Math.floor(u);
    v = Math.floor(v);
    return [
        y, u, v
    ]
}

function channelScale(pixelFmt, x, y) {
    return {
        channelOffsets: pixelFmt.channelOffsetsConstant.map(
            (cont, index) => cont + pixelFmt.channelOffsetsSize[index] *
                x * y),
        channelWidths: pixelFmt.channelWidths.map((width) => Math.floor(width * x)),
        channelPlaneWidths: pixelFmt.channelPlaneWidths.map((width) => Math.floor(width * x)),
        channelStrides: pixelFmt.channelStrides.map((width) => Math.floor(width * x)),
        channelSteps: pixelFmt.channelSteps.map((height) => height),
        channelHeights: pixelFmt.channelHeights.map((height) => Math.floor(height * y)),
        channelFourColors: pixelFmt.channelFourColors
    }
}


const scaleTests = [
    { from: { x: 64, y: 64 }, to: { x: 128, y: 128 } }, // Factor 2
    { from: { x: 128, y: 128 }, to: { x: 128, y: 128 } }, // Factor 1
    { from: { x: 128, y: 128 }, to: { x: 64, y: 64 } }, // Factor 0.5
    { from: { x: 32, y: 32 }, to: { x: 96, y: 96 } }, // Factor 3
    { from: { x: 192, y: 192 }, to: { x: 64, y: 64 } }, // Factor 1/3
    { from: { x: 64, y: 32 }, to: { x: 128, y: 64 } }, // Factor 2
    { from: { x: 128, y: 256 }, to: { x: 64, y: 128 } }, // Factor 0.5
]
const fourColors = [[255, 255, 0], [255, 0, 0], [0, 255, 0], [0, 0, 255]];
const pixelFormats = [
   { // RGBX
        channelOffsetsConstant: [0, 1, 2],
        channelOffsetsSize: [0, 0, 0],
        channelPlaneWidths: [4, 0, 0], // only used for allocation
        channelWidths: [1, 1, 1],
        channelStrides: [4, 4, 4], // scaled by width
        channelSteps: [4, 4, 4],
        channelHeights: [1, 1, 1],  // scaled by height
        channelFourColors: fourColors.map((col) => col), // just clone,
        format: 'RGBX'
    },
    { // I422
        channelOffsetsConstant: [0, 0, 0],
        channelOffsetsSize: [0, 1, 1.25],
        channelPlaneWidths: [1, 0.5, 0.5],
        channelWidths: [1, 0.5, 0.5],
        channelStrides: [1, 0.5, 0.5], // scaled by width
        channelSteps: [1, 1, 1],
        channelHeights: [1, 0.5, 0.5],  // scaled by height
        channelFourColors: fourColors.map((col) => rgb2yuv(col)), // just clone
        format: 'I420'
    }
]

for (const scale of scaleTests) {
    for (const pixelFmt of pixelFormats) {
        promise_test(async t => {
            const inputChannel = channelScale(pixelFmt, scale.from.x, scale.from.y);
            const inputData = createImageData(inputChannel);
            const inputFrame = new VideoFrame(inputData, {
                timestamp: 0,
                displayWidth: scale.from.x,
                displayHeight: scale.from.y,
                codedWidth: scale.from.x,
                codedHeight: scale.from.y,
                format: pixelFmt.format
            });
            const outputFrame = await executeCodecsOnce(inputFrame, scale.to);
            const outputArrayBuffer = new Uint8Array(outputFrame.allocationSize({ format: 'RGBX' }));
            const layout = await outputFrame.copyTo(outputArrayBuffer, { format: 'RGBX' });
            const stride = layout[0].stride
            const offset = layout[0].offset

            const error = testImageData(outputArrayBuffer, {
                channelOffsets: [offset, offset + 1, offset + 2],
                channelWidths: [outputFrame.codedWidth, outputFrame.codedWidth, outputFrame.codedWidth],
                channelStrides: [stride, stride, stride],
                channelSteps: [4, 4, 4],
                channelHeights: [outputFrame.codedHeight, outputFrame.codedHeight, outputFrame.codedHeight],
                channelFourColors: fourColors.map((col) => col)
            });
            outputFrame.close();
            assert_approx_equals(error, 0, 0.05, "Scaled Image differs too much! Scaling from "
                + scale.from.x + " x " + scale.from.y
                + " to "
                + scale.to.x + " x " + scale.to.y
                + " Format:" +
                pixelFmt.format
            );
        }, "Scaling Image in Encoding from "
        + scale.from.x + " x " + scale.from.y
        + " to "
        + scale.to.x + " x " + scale.to.y
        + " Format: " +
        pixelFmt.format + " ");
    }

}

