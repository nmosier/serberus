{
    loc = $2;
    n = $1;
    if (!(loc in dict)) {
	dict[loc] = 0;
    }
    dict[loc] += n;
}

END {
    for (loc in dict) {
	print dict[loc], loc;
    }
}
