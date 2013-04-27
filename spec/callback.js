function assert(b) {
    if(!b) {
        throw "Assertion failed";
    }
}

function compare_arrays(a1, a2) {
   for(var k in a1) {
      if(a1[k] !== a2[k]) {
         return false;
      }
   }
   for(var k in a2) {
      if(a2[k] !== a1[k]) {
         return false;
      }
   }
   return true;
}

function test() {
    assert(add2(3,4) == 7);
    assert(add3(3,4,-5) == 2);

    assert(fadd2(3.0,4.0) == 7.0);
    assert(fadd3(3.0,4.0,-5.0) == 2.0);

    assert(concat_strings("foo", "bar") == "foobar");

    a1 = [1,2,3];
    a2 = [4,5,6];
    a12 = [1,2,3,4,5,6];

    assert(compare_arrays(concat_arrays(a1, a2), a12));

    assert(negate(true) === false);
    assert(negate(false) === true);
    assert(typeof(negate(false)) == "boolean");

    return "ok";
}
