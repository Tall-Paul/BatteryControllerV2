difference(){
        union(){
            cube([130,26,4]);
            cube([60,40,4]);
            translate([2,2,0]){
                cube([126,22,8]);
            }
            translate([2,2,0]){
               cube([56,36,8]); 
            }        
        }
        union(){
            translate([4,4,2]){
                cube([122,18,6]);
            }
            translate([4,4,2]){
                 cube([52,32,6]);
            }
        }
        translate([106,0,0]){
            cube([20,20,8]);
        }
        translate([2,10,4]){
            cube([2,15,4]);
        }
        translate([25,36,4]){
            cube([10,2,4]);
        }
        translate([10,13,0]){
            cube([15,5,2]);
        }
}
