oomTest(function() {
    m = parseModule(`while (x && NaN) prototype; let x`);
    moduleLink(m);
    moduleEvaluate(m);
})
