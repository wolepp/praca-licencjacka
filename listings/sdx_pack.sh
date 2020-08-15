sdx_pack -header nn.hpp -lib libnn.a \              
         -func nn -map input=s_axi_AXILiteS:in:1024 \
                  -map output=s_axi_AXILiteS:out:2048 \
	 -func-end \
         -ip ip/component.xml \
         -control ap_ctrl_hs=s_axi_AXILiteS:0 \
         -primary-clk ap_clk=13.333 \
         -target-family zynquplus \
         -target-cpu cortex-a53 \
         -target-os linux



