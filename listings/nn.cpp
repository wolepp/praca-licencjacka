//
//    rfnoc-hls-neuralnet: Vivado HLS code for neural-net building blocks
//
//    Copyright (C) 2017 EJ Kreinar
//
//    This program is free software: you can redistribute it and/or modify
//    it under the terms of the GNU General Public License as published by
//    the Free Software Foundation, either version 3 of the License, or
//    (at your option) any later version.
//
//    This program is distributed in the hope that it will be useful,
//    but WITHOUT ANY WARRANTY; without even the implied warranty of
//    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
//    GNU General Public License for more details.
//
//    You should have received a copy of the GNU General Public License
//    along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <iostream>

#include "nn.h"
#include "parameters.h"

void nn(c_input_t input[N_INPUT_1_1], c_output_t output[N_LAYER_6])
{
	#pragma HLS CLOCK domain=75
    //hls-fpga-machine-learning insert IO
	#pragma HLS INTERFACE s_axilite depth=784 port=input
	#pragma HLS INTERFACE s_axilite depth=10 port=output
	#pragma HLS INTERFACE s_axilite port=return
	#pragma HLS DATAFLOW

	// przerobienie na input_t
	input_t input1[N_INPUT_1_1];
    #pragma HLS ARRAY_RESHAPE variable=input1 complete dim=0
	for(int i=0; i<N_INPUT_1_1; i++) {
	#pragma HLS pipeline II=1
		input1[i] = (input[i] < 140) ? 255 : 0;
	}


#ifndef __SYNTHESIS__
    static bool loaded_weights = false;
    if (!loaded_weights) {
        //hls-fpga-machine-learning insert load weights
    	nnet::load_weights_from_txt<dense_weight_t, 9408>(w2, "w2.txt");
    	nnet::load_weights_from_txt<dense_bias_t, 12>(b2, "b2.txt");
    	nnet::load_weights_from_txt<dense_1_weight_t, 480>(w4, "w4.txt");
    	nnet::load_weights_from_txt<dense_1_bias_t, 40>(b4, "b4.txt");
    	nnet::load_weights_from_txt<dense_2_weight_t, 400>(w6, "w6.txt");
    	nnet::load_weights_from_txt<dense_2_bias_t, 10>(b6, "b6.txt");
        loaded_weights = true;
    }
#endif

    // ****************************************
    // NETWORK INSTANTIATION
    // ****************************************

    //hls-fpga-machine-learning insert layers

    layer2_t layer2_out[N_LAYER_2];
    #pragma HLS ARRAY_PARTITION variable=layer2_out complete dim=0
    nnet::dense_large<input_t, layer2_t, config2>(input1, layer2_out, w2, b2);

    layer3_t layer3_out[N_LAYER_2];
    #pragma HLS ARRAY_PARTITION variable=layer3_out complete dim=0
    nnet::relu<layer2_t, layer3_t, relu_config3>(layer2_out, layer3_out);

    layer4_t layer4_out[N_LAYER_4];
    #pragma HLS ARRAY_PARTITION variable=layer4_out complete dim=0
    nnet::dense_large<layer3_t, layer4_t, config4>(layer3_out, layer4_out, w4, b4);

    layer5_t layer5_out[N_LAYER_4];
    #pragma HLS ARRAY_PARTITION variable=layer5_out complete dim=0
    nnet::relu<layer4_t, layer5_t, relu_config5>(layer4_out, layer5_out);

    layer6_t layer6_out[N_LAYER_6];
    #pragma HLS ARRAY_PARTITION variable=layer6_out complete dim=0
    nnet::dense_large<layer5_t, layer6_t, config6>(layer5_out, layer6_out, w6, b6);

    result_t layer7_out[N_LAYER_6];
	#pragma HLS ARRAY_PARTITION variable=layer7_out complete dim=0
    nnet::softmax<layer6_t, result_t, softmax_config7>(layer6_out, layer7_out);

    for(int i=0; i<N_LAYER_6; i++) {
//#pragma HLS pipeline II=1
    	output[i] = layer7_out[i].to_float();
    }


    //return (mod_result_t) index_of_max_val<result_t, N_LAYER_6>(layer7_out);
}
