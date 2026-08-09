B=/c/Claude/uPython/femm-qt/build/bin/femm_headless.exe
run(){ # test compare golden fq
  $B -run "C:/Claude/uPython/femm-qt/testing/$1" > "/tmp/$4.raw" 2>&1
  grep -vE "FEMM headless|IPC:|Executing:|Script complete" "/tmp/$4.raw" > "$4"
  printf "%-26s " "${1%.py}:"; python3 "$2" "$3" "$4" 2>&1 | head -1
}
run MOBlockIntegralTest.py   MOBlockIntegralCompare.py   OF_result.txt      fq_result.txt
run ElecBlockIntegralTest.py EOBlockIntegralCompare.py   OF_elec_result.txt fq_elec_result.txt
run HeatBlockIntegralTest.py HeatBlockIntegralCompare.py OF_heat_result.txt fq_heat_result.txt
run CurrBlockIntegralTest.py CurrBlockIntegralCompare.py OF_curr_result.txt fq_curr_result.txt
run MOLineIntegralTest.py    MOLineIntegralCompare.py    OF_line_result.txt  fq_line_result.txt
run EOLineIntegralTest.py    EOLineIntegralCompare.py    OF_eline_result.txt fq_eline_result.txt
run CILineIntegralTest.py    CILineIntegralCompare.py    OF_iline_result.txt fq_iline_result.txt
run HILineIntegralTest.py    HILineIntegralCompare.py    OF_hline_result.txt fq_hline_result.txt
run MOGapIntegralTest.py     MOGapIntegralCompare.py     OF_gap_result.txt   fq_gap_result.txt
run MOCircuitPropTest.py     MOCircuitPropCompare.py     OF_circ_result.txt  fq_circ_result.txt
run EOCondPropTest.py        CondPropCompare.py          OF_eic_result.txt   fq_eic_result.txt
run COCondPropTest.py        CondPropCompare.py          OF_cic_result.txt   fq_cic_result.txt
run HOCondPropTest.py        CondPropCompare.py          OF_hic_result.txt   fq_hic_result.txt
run MOPointValueTest.py      MOPointValueCompare.py      OF_pv_result.txt    fq_pv_result.txt
run MOBHCurveTest.py         MOBHCurveCompare.py         OF_bh_result.txt    fq_bh_result.txt
run EOPointValueTest.py      ScalarPointValueCompare.py  OF_eopv_result.txt  fq_eopv_result.txt
run COPointValueTest.py      ScalarPointValueCompare.py  OF_copv_result.txt  fq_copv_result.txt
run HOPointValueTest.py      ScalarPointValueCompare.py  OF_hopv_result.txt  fq_hopv_result.txt

printf "%-26s " "FluxConstraint:"; python3 FluxConstraintTest.py "$B" | tail -1
printf "%-26s " "Transient:"; python3 TransientTest.py "${B%femm_headless.exe}fkn.exe" "$B" | tail -1
printf "%-26s " "IncrementalLorentz:"; python3 IncrementalLorentzTest.py "$B" | tail -1
printf "%-26s " "FQReference:"; python3 FQReferenceCompare.py | tail -1
echo "ALL DONE"
