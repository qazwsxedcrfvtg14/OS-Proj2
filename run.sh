./user_program/master data/file1_in fcntl &
./user_program/slave data/file1_out fcntl 127.0.0.1
diff data/file1_in data/file1_out

./user_program/master data/file2_in fcntl &
./user_program/slave data/file2_out fcntl 127.0.0.1
diff data/file2_in data/file2_out

./user_program/master data/file3_in fcntl &
./user_program/slave data/file3_out fcntl 127.0.0.1
diff data/file3_in data/file3_out

./user_program/master data/file1_in mmap &
./user_program/slave data/file1_out mmap 127.0.0.1
diff data/file1_in data/file1_out

./user_program/master data/file2_in mmap &
./user_program/slave data/file2_out mmap 127.0.0.1
diff data/file2_in data/file2_out

./user_program/master data/file3_in mmap &
./user_program/slave data/file3_out mmap 127.0.0.1
diff data/file3_in data/file3_out

