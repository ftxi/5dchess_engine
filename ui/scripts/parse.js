export function parse_FEN(fen, size_x, size_y) 
{
    const rows = fen.split('/');
    if (rows.length !== size_x) 
    {
        throw new Error(`Invalid FEN format: input must have exactly ${size_x} rows.`);
    }

    return rows.map(row => {
        let parsedRow = [];

        // Parse each character in the row
        for (let char of row) 
        {
        	if(isNaN(char))
            {
                if ("BCDKNPSQRUWYbcdknpsqruwy".includes(char)) 
                {
                    parsedRow.push(char);
            	}
                else
                {
              	    throw new Error('Invalid FEN format: invalid piece:'+ char);
                }
            }
            else
            {
                let emptySquares = parseInt(char);
                parsedRow.push(...Array(emptySquares).fill('1'));
            }
        }
        if (parsedRow.length != size_y)
        {
            throw new Error(`Invalid FEN format: each row must have exactly ${size_y} squares. `+row);
        }
        return parsedRow;
    });
}
