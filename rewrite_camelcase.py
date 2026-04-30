import os
import re

replacements = {
    # chunk_sorter.hpp
    "start_offset": "startOffset",
    "max_inflight": "maxInflight",
    "sort_range_to_runs": "sortRangeToRuns",
    "run_path": "runPath",
    "write_ok": "writeOk",
    "in_flight": "inFlight",
    "estimated_records": "estimatedRecords",
    "after_header": "afterHeader",
    "rec_start": "recStart",
    "bytes_read": "bytesRead",

    # kway_merger.hpp
    "delete_src": "deleteSource",
    "merge_pass": "mergePass",
    "move_or_copy_run": "moveOrCopyRun",
    "kway_merge": "kwayMerge",

    # record.hpp
    "skip_payload": "skipPayload",
    "write_record": "writeRecord",
    "read_header": "readHeader",

    # temp_dir.hpp
    "base_dir": "baseDir",
    "now_tp": "nowTp",
    "now_count": "nowCount",
    "dir_name": "dirName",
    "should_delete": "shouldDelete",

    # generate.cpp
    "key_rnd": "keyRnd",
    "len_rnd": "lenRnd",
    "payload_buf": "payloadBuf",

    # verify.cpp
    "min_key": "minKey",
    "max_key": "maxKey",
    "hash_sum": "hashSum",
    "hash_xor": "hashXor",
    "order_errors": "orderErrors",
    "fnv1a_update": "fnv1aUpdate",
    "scan_file": "scanFile",
    "check_order": "checkOrder",
    "prev_key": "prevKey",
    "rec_hash": "recHash",
    "print_stats": "printStats",
    "same_records": "sameRecords",

    # fastflow
    "ff_sort_to_runs": "ffSortToRuns",
    "ff_kway_merge": "ffKwayMerge",

    # mpi
    "file_size": "fileSize",
    "compute_record_boundaries": "computeRecordBoundaries",
    "file_sz": "fileSize",
    "nprocs": "numProcs",
    "next_rank": "nextRank",
    "mpi_send_file": "mpiSendFile",
    "mpi_recv_file": "mpiRecvFile",
    "tag_size": "tagSize",
    "tag_data": "tagData",
    "batch_64": "batch64",
    "bytes_written": "bytesWritten",
    "rank_prefix": "rankPrefix",
    "work_tmp": "workTmp",
    "my_tmp": "myTmp",
    "t_start": "tStart",
    "t_end": "tEnd",
    "total_bytes": "totalBytes",
    "my_start": "myStart",
    "my_end": "myEnd",
    "local_sorted": "localSorted",
    "pair_runs": "pairRuns",
    "group_size": "groupSize",
    "my_group": "myGroup",
    "is_receiver": "isReceiver",
    "is_sender": "isSender",
    "recv_path": "recvPath",
    "merged_path": "mergedPath",
    "current_file": "currentFile",

    # omp
    "omp_kway_merge": "ompKwayMerge",
    "keep_runs": "keepRuns",
    "par_merge": "parallelMerge",
    "delSrc": "deleteSource",
    "out_path": "outPath"
}

def process_file(filepath):
    with open(filepath, 'r', encoding='utf-8') as f:
        content = f.read()

    # Regex to match string literals, C-style comments, C++ style comments, or anything else
    pattern = re.compile(
        r'(?P<string>"(?:\\.|[^"\\])*")|'
        r'(?P<char>\'(?:\\.|[^\'\\])*\')|'
        r'(?P<comment_c>/\*.*?\*/)|'
        r'(?P<comment_cpp>//[^\n]*)|'
        r'(?P<other>.+?)',
        re.DOTALL
    )

    def replacer(match):
        if match.group('other'):
            text = match.group('other')
            for old, new in replacements.items():
                text = re.sub(r'\b' + re.escape(old) + r'\b', new, text)
            return text
        else:
            return match.group(0)

    new_content = pattern.sub(replacer, content)

    if new_content != content:
        with open(filepath, 'w', encoding='utf-8') as f:
            f.write(new_content)
        print(f"Updated {filepath}")

if __name__ == '__main__':
    target_dir = r"\\wsl.localhost\Ubuntu\home\matti\spm projects\spm"
    for root, dirs, files in os.walk(target_dir):
        for file in files:
            if file.endswith('.hpp') or file.endswith('.cpp'):
                process_file(os.path.join(root, file))
    print("Done rewriting variables to camelCase.")
