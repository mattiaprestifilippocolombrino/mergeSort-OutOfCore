trials=5
n_records=($((1024*1024*2)) $((5*1024*2)) )
max_payloads=($((5*1024))   $((1024*1024)) )  

memory_limit=($((1*1024*1024*1024)) )


make cleanall
make payload_generator

if ! [ -f "test_files" ]; then
    mkdir -p test_files
fi

n_combs=${#n_records[@]}
for j in $(seq 0 $((n_combs-1)) ); do
    mp=${max_payloads[$j]}
    nr=${n_records[$j]}
    echo "checking ${mp} and ${nr}"
    if ! [ -f "test_files/file_mp${mp}_nr${nr}.pms" ]; then
        echo "generating..."
        ./utilities/payload_generator -o test_files/file_mp${mp}_nr${nr}.pms -p ${mp} -r ${nr} -v 0
    fi
    echo "test_files/file_mp${mp}_nr${nr}.pms is present"
done

echo "files generated!"
echo "starting experiments:"

make cleanall
make ms_sequential

output_file="$1"
touch "$output_file"
echo "" > "$output_file"
echo "iteration,max_payload_size,records_number,memory_limit,time(ms)" >> "$output_file"

for i in $(seq 1 $trials); do
    for j in $(seq 0  $((n_combs-1))); do
        mp=${max_payloads[$j]}
        nr=${n_records[$j]}
        for m in "${memory_limit[@]}";do
            echo "iteration=$i max_payload=$mp records_number=$nr memory_limit=$m"
            output=$(srun --time=00:20:00  ./ms_sequential -i test_files/file_mp${mp}_nr${nr}.pms -o test_files/file_mp${mp}_nr${nr}_out.pms -m $m -v 0 )
            echo "$output"
            time_ms=$(echo "$output" | grep 'time(ms):' | awk -F ':' '{print $2}')
            echo "$i,$mp,$nr,$m,$time_ms" >> "$output_file"
            rm test_files/file_mp${mp}_nr${nr}_out.pms
        done
    done
done

echo "experiments done!"