The compression algorithm performs the following loop until it hits the end of the input stream:

- `p` is a pointer to the input data stream
- (1) find the longest string starting at position `p` that is contained in the dictionary
- (2) search the look-up window for the most frequently repeated string starting at position `p`
- select either (1) or (2) according to the longer string
- encode this information and possibly add the string (2) to the dictionary
- advance the `p`

All information is encoded using a context arithmetic encoder.