#!/bin/bash

trials=5

n_records=(   $((1024*1024*2)) $((1024*1024*2)) $((1024*1024*2)) $(((3/2)*1024*2))   $((5*1024*2))     $((10*1024*2)))
max_payloads=($(((3/2)*1024))    $((5*1024))      $((10*1024))     $((1024*1024))    $((1024*1024))    $((1024*1024)))  
num_nodes=(    2               4                8                2                 4                 8             )


num_threads=(1 2 4 8 16 32)


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

output_file="$1"
touch "$output_file"
echo "" > "$output_file"
echo "iteration,num_threads,num_nodes,max_payload_size,records_number,time(ms)" >> "$output_file"

make cleanall
make mpi_multinode
for i in $(seq 1 $trials); do
    for j in $(seq 0  $((n_combs-1))); do
        mp=${max_payloads[$j]}
        nr=${n_records[$j]}
        nn=${num_nodes[$j]}
        for nt in "${num_threads[@]}"; do
            echo "iteration=$i max_payload=$mp records_number=$nr  num_threads=$nt num_nodes=$nn"
            output=$(srun --mpi=pmix --cpu-bind=none -N $nn  --time=00:30:00 --mpi=pmix $(pwd)/mpi_multinode -i test_files/file_mp${mp}_nr${nr}.pms -o test_files/file_mp${mp}_nr${nr}_out.pms -t ${nt} -v 0)
            echo "$output"
            time_ms=$(echo "$output" | grep 'time(ms):' | awk -F ':' '{print $2}')
            echo "$i,$nt,$nn,$mp,$nr,$time_ms" >> "$output_file"
            rm $(pwd)/test_files/file_mp${mp}_nr${nr}_out.pms
        done
    done
done

echo "experiments done!"