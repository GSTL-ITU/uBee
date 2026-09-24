`timescale 1ns / 1ps
//////////////////////////////////////////////////////////////////////////////////
// Company: 
// Engineer: 
// 
// Create Date: 09/10/2026 06:01:55 PM
// Design Name: 
// Module Name: interrupt_order_manager
// Project Name: 
// Target Devices: 
// Tool Versions: 
// Description: 
// 
// Dependencies: 
// 
// Revision:
// Revision 0.01 - File Created
// Additional Comments:
// 
//////////////////////////////////////////////////////////////////////////////////


module interrupt_order_manager(
input wire in_0 , in_1 , in_2,
input wire in_3 , in_4 , in_5,
input wire in_6 , in_7 , in_8,
input wire in_9 , in_10, in_11,
input wire in_12, in_13, in_14,
input wire in_15, 
output wire [15:0] interrupt_array
    );
    
    assign interrupt_array = {in_15,  // array[15]
                           in_14,  // array[14]
                           in_13,  // array[13]
                           in_12,  // array[12]
                           in_11,  // array[11]
                           in_10,  // array[10]
                           in_9,   // array[9]
                           in_8,   // array[8] 
                           in_7,   // array[7]
                           in_6,   // array[6]
                           in_5,   // array[5]
                           in_4,   // array[4]
                           in_3,   // array[3]
                           in_2,   // array[2]
                           in_1,   // array[1]
                           in_0};  // array[0]
    
endmodule


















